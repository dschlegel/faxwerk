#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

#include <optional>

namespace faxwerk {

// A parsed WWW-Authenticate / Proxy-Authenticate challenge (RFC 2617, RFC 7616).
struct DigestChallenge
{
    QString realm;
    QString nonce;
    QString opaque;
    QString algorithm;  // as sent by the server; empty means MD5
    QStringList qop;    // offered qop values, e.g. {"auth", "auth-int"}
    bool stale = false;

    static std::optional<DigestChallenge> parse(const QString &headerValue);
};

// Computes Authorization / Proxy-Authorization header values.
//
// Supported algorithms: MD5, MD5-sess, SHA-256, SHA-256-sess.
// Supported qop: auth, auth-int and the legacy RFC 2069 mode without qop.
// The nonce count (nc) is tracked per nonce.
class DigestAuth
{
public:
    enum class Algorithm { Md5, Md5Sess, Sha256, Sha256Sess };

    static std::optional<Algorithm> algorithmFromString(const QString &name);

    // The raw "response" value of the digest computation.
    static QByteArray computeResponse(Algorithm algorithm,
                                      const QString &username, const QString &realm,
                                      const QString &password, const QString &method,
                                      const QString &digestUri, const QString &nonce,
                                      const QString &nc, const QString &cnonce,
                                      const QString &qop, const QByteArray &body = QByteArray());

    DigestAuth() = default;
    DigestAuth(const QString &username, const QString &password)
        : m_username(username), m_password(password) {}

    void setCredentials(const QString &username, const QString &password)
    {
        m_username = username;
        m_password = password;
    }

    // Builds a header value answering the challenge. Returns an empty string
    // if the challenge uses an unsupported algorithm or qop.
    // cnonce may be supplied for reproducible results (tests).
    QString authorize(const DigestChallenge &challenge, const QString &method,
                      const QString &digestUri, const QByteArray &body = QByteArray(),
                      const QString &cnonce = QString());

    // Number of times authorize() has been called for the nonce.
    int nonceCount(const QString &nonce) const { return m_nonceCounts.value(nonce, 0); }

private:
    QString m_username;
    QString m_password;
    QHash<QString, int> m_nonceCounts;
};

} // namespace faxwerk
