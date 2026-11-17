#pragma once

#include <QByteArray>
#include <QString>

// Checks an OpenPGP detached signature (v4, RSA, SHA-256/384/512) against one pinned key.
// Just enough OpenPGP to verify yt-dlp's release checksums; anything else is refused.
namespace Pgp {

struct Result
{
    bool ok = false;
    QString why;   // reason when not ok
};

// key: ASCII-armoured or binary public key. fingerprint: 40 hex digits the key must have.
Result verifyDetached(const QByteArray &data, const QByteArray &signature,
                      const QByteArray &key, const QString &fingerprint);

}
