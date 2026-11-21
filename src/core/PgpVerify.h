#pragma once

#include <QByteArray>
#include <QString>

// Checks an OpenPGP detached signature (v4, RSA, SHA-256/384/512) against one pinned key.
// Just enough OpenPGP to verify yt-dlp's release checksums; anything else is refused.
namespace Pgp {

struct Result
{
    bool ok = false;
    QString why;        // reason when not ok
    qint64 created = 0; // when the signature was made, seconds since 1970
};

// key: ASCII-armoured or binary public key. fingerprint: 40 hex digits the key must have.
// The signature must say when it was made, not be dated in the future and not have expired.
Result verifyDetached(const QByteArray &data, const QByteArray &signature,
                      const QByteArray &key, const QString &fingerprint);

}
