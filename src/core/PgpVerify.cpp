#include "PgpVerify.h"

#include <QCryptographicHash>
#include <QList>

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <openssl/rsa.h>

#include <memory>

namespace Pgp {
namespace {

struct Packet { int tag; QByteArray body; };
struct Key { QByteArray n, e, fingerprint; };   // fingerprint as 20 raw bytes

// Reads the body of an ASCII-armoured block; binary input is returned unchanged.
QByteArray dearmor(const QByteArray &in)
{
    if (!in.trimmed().startsWith("-----BEGIN"))
        return in;
    QByteArray base64;
    bool inBody = false;
    for (QByteArray line : in.split('\n')) {
        line = line.trimmed();
        if (line.startsWith("-----END") || (inBody && line.startsWith('=')))
            break;
        if (inBody)
            base64 += line;
        else if (line.isEmpty())
            inBody = true;   // headers end at the first blank line
    }
    return QByteArray::fromBase64(base64);
}

// Splits OpenPGP packets (old and new header formats, no partial lengths).
bool packets(const QByteArray &raw, QList<Packet> &out)
{
    qsizetype i = 0;
    const auto byte = [&](qsizetype at) { return quint8(raw.at(at)); };
    while (i < raw.size()) {
        const quint8 ctb = byte(i++);
        if (!(ctb & 0x80))
            return false;
        int tag;
        quint64 len = 0;
        if (ctb & 0x40) {   // new format
            tag = ctb & 0x3f;
            if (i >= raw.size())
                return false;
            const quint8 o1 = byte(i++);
            if (o1 < 192)
                len = o1;
            else if (o1 < 224 && i < raw.size())
                len = ((o1 - 192) << 8) + byte(i++) + 192;
            else if (o1 == 255 && i + 4 <= raw.size()) {
                len = quint64(byte(i)) << 24 | byte(i + 1) << 16 | byte(i + 2) << 8 | byte(i + 3);
                i += 4;
            } else
                return false;
        } else {   // old format
            tag = (ctb >> 2) & 0x0f;
            const int sizes[] = {1, 2, 4, 0};
            const int n = sizes[ctb & 3];
            if (!n || i + n > raw.size())
                return false;
            for (int k = 0; k < n; ++k)
                len = len << 8 | byte(i++);
        }
        if (len > quint64(raw.size() - i))
            return false;
        out.append({tag, raw.mid(i, qsizetype(len))});
        i += qsizetype(len);
    }
    return !out.isEmpty();
}

// Multi-precision integer: 2-byte bit count, then the bytes.
bool readMpi(const QByteArray &b, qsizetype &at, QByteArray &out)
{
    if (at + 2 > b.size())
        return false;
    const int bits = quint8(b[at]) << 8 | quint8(b[at + 1]);
    const qsizetype bytes = (bits + 7) / 8;
    at += 2;
    if (bytes == 0 || at + bytes > b.size())
        return false;
    out = b.mid(at, bytes);
    at += bytes;
    return true;
}

// v4 RSA public key or subkey packet.
bool readKey(const Packet &p, Key &key)
{
    const QByteArray &b = p.body;
    if ((p.tag != 6 && p.tag != 14) || b.size() < 6 || quint8(b[0]) != 4)
        return false;
    const int algo = quint8(b[5]);
    if (algo != 1 && algo != 3)   // RSA, RSA sign-only
        return false;
    qsizetype at = 6;
    if (!readMpi(b, at, key.n) || !readMpi(b, at, key.e))
        return false;
    // v4 fingerprint: SHA-1 over 0x99, 2-byte length, key material.
    QByteArray framed;
    framed.append(char(0x99)).append(char(at >> 8)).append(char(at & 0xff)).append(b.left(at));
    key.fingerprint = QCryptographicHash::hash(framed, QCryptographicHash::Sha1);
    return true;
}

const EVP_MD *digestFor(int hashAlgo, QCryptographicHash::Algorithm &qt)
{
    switch (hashAlgo) {
    case 8: qt = QCryptographicHash::Sha256; return EVP_sha256();
    case 9: qt = QCryptographicHash::Sha384; return EVP_sha384();
    case 10: qt = QCryptographicHash::Sha512; return EVP_sha512();
    default: return nullptr;   // MD5 and SHA-1 are not accepted
    }
}

bool rsaVerify(const Key &key, const EVP_MD *md, const QByteArray &digest, QByteArray sig)
{
    if (sig.size() > key.n.size())
        return false;
    sig.prepend(QByteArray(key.n.size() - sig.size(), '\0'));   // MPIs drop leading zeros

    using Bn = std::unique_ptr<BIGNUM, decltype(&BN_free)>;
    Bn n(BN_bin2bn(reinterpret_cast<const unsigned char *>(key.n.data()), int(key.n.size()), nullptr), BN_free);
    Bn e(BN_bin2bn(reinterpret_cast<const unsigned char *>(key.e.data()), int(key.e.size()), nullptr), BN_free);
    std::unique_ptr<OSSL_PARAM_BLD, decltype(&OSSL_PARAM_BLD_free)> bld(OSSL_PARAM_BLD_new(), OSSL_PARAM_BLD_free);
    if (!n || !e || !bld || !OSSL_PARAM_BLD_push_BN(bld.get(), OSSL_PKEY_PARAM_RSA_N, n.get())
        || !OSSL_PARAM_BLD_push_BN(bld.get(), OSSL_PKEY_PARAM_RSA_E, e.get()))
        return false;
    std::unique_ptr<OSSL_PARAM, decltype(&OSSL_PARAM_free)> params(OSSL_PARAM_BLD_to_param(bld.get()), OSSL_PARAM_free);
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> make(EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr), EVP_PKEY_CTX_free);
    EVP_PKEY *raw = nullptr;
    if (!params || !make || EVP_PKEY_fromdata_init(make.get()) <= 0
        || EVP_PKEY_fromdata(make.get(), &raw, EVP_PKEY_PUBLIC_KEY, params.get()) <= 0)
        return false;
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(raw, EVP_PKEY_free);

    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(EVP_PKEY_CTX_new(pkey.get(), nullptr), EVP_PKEY_CTX_free);
    return ctx && EVP_PKEY_verify_init(ctx.get()) > 0
        && EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_PKCS1_PADDING) > 0
        && EVP_PKEY_CTX_set_signature_md(ctx.get(), md) > 0
        && EVP_PKEY_verify(ctx.get(), reinterpret_cast<const unsigned char *>(sig.constData()), size_t(sig.size()),
                           reinterpret_cast<const unsigned char *>(digest.constData()), size_t(digest.size())) == 1;
}

}

Result verifyDetached(const QByteArray &data, const QByteArray &signature, const QByteArray &keyData,
                      const QString &fingerprint)
{
    // The key: the pinned primary key and any subkeys bundled with it.
    QList<Packet> keyPackets;
    if (!packets(dearmor(keyData), keyPackets))
        return {false, "The built-in signing key couldn't be read"};
    QList<Key> keys;
    bool pinned = false;
    for (const Packet &p : keyPackets) {
        Key k;
        if (!readKey(p, k))
            continue;
        if (p.tag == 6 && k.fingerprint.toHex().compare(fingerprint.toLatin1(), Qt::CaseInsensitive) == 0)
            pinned = true;
        keys.append(k);
    }
    if (!pinned)
        return {false, "The built-in signing key isn't the pinned one"};

    // The signature packet.
    QList<Packet> sigPackets;
    if (!packets(dearmor(signature), sigPackets) || sigPackets.size() != 1 || sigPackets[0].tag != 2)
        return {false, "The signature file is malformed"};
    const QByteArray &s = sigPackets[0].body;
    if (s.size() < 6 || quint8(s[0]) != 4)
        return {false, "Only version 4 signatures are supported"};
    const int sigType = quint8(s[1]), pkAlgo = quint8(s[2]), hashAlgo = quint8(s[3]);
    if (sigType != 0x00 && sigType != 0x01)
        return {false, "The signature isn't a document signature"};
    if (pkAlgo != 1 && pkAlgo != 3)
        return {false, "The signature isn't RSA"};
    QCryptographicHash::Algorithm qtHash;
    const EVP_MD *md = digestFor(hashAlgo, qtHash);
    if (!md)
        return {false, "The signature uses a weak or unknown hash"};

    qsizetype at = 4;
    const int hashedLen = quint8(s[at]) << 8 | quint8(s[at + 1]);
    at += 2 + hashedLen;
    if (at + 2 > s.size())
        return {false, "The signature file is malformed"};
    const QByteArray hashedPart = s.left(at);   // version .. end of hashed subpackets
    const int unhashedLen = quint8(s[at]) << 8 | quint8(s[at + 1]);
    at += 2 + unhashedLen;
    if (at + 2 > s.size())
        return {false, "The signature file is malformed"};
    const QByteArray left16 = s.mid(at, 2);
    at += 2;
    QByteArray sigMpi;
    if (!readMpi(s, at, sigMpi))
        return {false, "The signature file is malformed"};

    // Hashed: the data, the hashed part of the packet, then the v4 trailer.
    QCryptographicHash h(qtHash);
    h.addData(data);
    h.addData(hashedPart);
    const quint32 n = quint32(hashedPart.size());
    const char trailer[] = {4, char(0xff), char(n >> 24), char(n >> 16), char(n >> 8), char(n)};
    h.addData(QByteArrayView(trailer, 6));
    const QByteArray digest = h.result();
    if (digest.left(2) != left16)
        return {false, "The file doesn't match its signature"};

    for (const Key &k : keys)
        if (rsaVerify(k, md, digest, sigMpi))
            return {true, {}};
    return {false, "The file doesn't match its signature"};
}

}
