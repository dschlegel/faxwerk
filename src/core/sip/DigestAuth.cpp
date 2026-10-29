#include "DigestAuth.h"

#include "SipMessage.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

namespace faxwerk {

namespace {

QByteArray hashHex(DigestAuth::Algorithm algorithm, const QByteArray &data)
{
    const bool sha = algorithm == DigestAuth::Algorithm::Sha256
                     || algorithm == DigestAuth::Algorithm::Sha256Sess;
    return QCryptographicHash::hash(data, sha ? QCryptographicHash::Sha256 : QCryptographicHash::Md5)
        .toHex();
}

QString algorithmName(DigestAuth::Algorithm algorithm)
{
    switch (algorithm) {
    case DigestAuth::Algorithm::Md5: return QStringLiteral("MD5");
    case DigestAuth::Algorithm::Md5Sess: return QStringLiteral("MD5-sess");
    case DigestAuth::Algorithm::Sha256: return QStringLiteral("SHA-256");
    case DigestAuth::Algorithm::Sha256Sess: return QStringLiteral("SHA-256-sess");
    }
    return QStringLiteral("MD5");
}

} // namespace

std::optional<DigestChallenge> DigestChallenge::parse(const QString &headerValue)
{
    const QString value = headerValue.trimmed();
    if (!value.startsWith(QLatin1String("Digest"), Qt::CaseInsensitive))
        return std::nullopt;

    DigestChallenge challenge;
    // auth-param list: name=value pairs separated by commas, values may be quoted.
    for (const QString &part : sip::splitList(value.mid(6))) {
        const int eq = part.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString name = part.left(eq).trimmed().toLower();
        const QString val = sip::unquote(part.mid(eq + 1).trimmed());
        if (name == QLatin1String("realm"))
            challenge.realm = val;
        else if (name == QLatin1String("nonce"))
            challenge.nonce = val;
        else if (name == QLatin1String("opaque"))
            challenge.opaque = val;
        else if (name == QLatin1String("algorithm"))
            challenge.algorithm = val;
        else if (name == QLatin1String("stale"))
            challenge.stale = val.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
        else if (name == QLatin1String("qop")) {
            for (const QString &q : val.split(QLatin1Char(','), Qt::SkipEmptyParts))
                challenge.qop.append(q.trimmed().toLower());
        }
    }
    if (challenge.nonce.isEmpty())
        return std::nullopt;
    return challenge;
}

std::optional<DigestAuth::Algorithm> DigestAuth::algorithmFromString(const QString &name)
{
    const QString n = name.trimmed().toUpper();
    if (n.isEmpty() || n == QLatin1String("MD5"))
        return Algorithm::Md5;
    if (n == QLatin1String("MD5-SESS"))
        return Algorithm::Md5Sess;
    if (n == QLatin1String("SHA-256"))
        return Algorithm::Sha256;
    if (n == QLatin1String("SHA-256-SESS"))
        return Algorithm::Sha256Sess;
    return std::nullopt;
}

QByteArray DigestAuth::computeResponse(Algorithm algorithm, const QString &username,
                                       const QString &realm, const QString &password,
                                       const QString &method, const QString &digestUri,
                                       const QString &nonce, const QString &nc,
                                       const QString &cnonce, const QString &qop,
                                       const QByteArray &body)
{
    QByteArray ha1 = hashHex(algorithm, username.toUtf8() + ':' + realm.toUtf8() + ':' + password.toUtf8());
    if (algorithm == Algorithm::Md5Sess || algorithm == Algorithm::Sha256Sess)
        ha1 = hashHex(algorithm, ha1 + ':' + nonce.toUtf8() + ':' + cnonce.toUtf8());

    QByteArray a2 = method.toUtf8() + ':' + digestUri.toUtf8();
    if (qop == QLatin1String("auth-int"))
        a2 += ':' + hashHex(algorithm, body);
    const QByteArray ha2 = hashHex(algorithm, a2);

    if (qop.isEmpty())
        return hashHex(algorithm, ha1 + ':' + nonce.toUtf8() + ':' + ha2);
    return hashHex(algorithm, ha1 + ':' + nonce.toUtf8() + ':' + nc.toUtf8() + ':'
                                  + cnonce.toUtf8() + ':' + qop.toUtf8() + ':' + ha2);
}

QString DigestAuth::authorize(const DigestChallenge &challenge, const QString &method,
                              const QString &digestUri, const QByteArray &body,
                              const QString &cnonceOverride)
{
    const auto algorithm = algorithmFromString(challenge.algorithm);
    if (!algorithm)
        return QString();

    QString qop;
    if (challenge.qop.contains(QLatin1String("auth")))
        qop = QStringLiteral("auth");
    else if (challenge.qop.contains(QLatin1String("auth-int")))
        qop = QStringLiteral("auth-int");
    else if (!challenge.qop.isEmpty())
        return QString(); // only unknown qop values offered

    const bool sess = *algorithm == Algorithm::Md5Sess || *algorithm == Algorithm::Sha256Sess;
    QString cnonce = cnonceOverride;
    if (cnonce.isEmpty() && (!qop.isEmpty() || sess))
        cnonce = QString::number(QRandomGenerator::global()->generate64(), 16);

    const int count = ++m_nonceCounts[challenge.nonce];
    const QString nc = QStringLiteral("%1").arg(count, 8, 16, QLatin1Char('0'));

    const QByteArray response = computeResponse(*algorithm, m_username, challenge.realm, m_password,
                                                method, digestUri, challenge.nonce, nc, cnonce,
                                                qop, body);

    QStringList parts;
    parts << QStringLiteral("username=%1").arg(sip::quote(m_username))
          << QStringLiteral("realm=%1").arg(sip::quote(challenge.realm))
          << QStringLiteral("nonce=%1").arg(sip::quote(challenge.nonce))
          << QStringLiteral("uri=%1").arg(sip::quote(digestUri))
          << QStringLiteral("response=%1").arg(sip::quote(QString::fromLatin1(response)));
    if (!challenge.algorithm.isEmpty())
        parts << QStringLiteral("algorithm=%1").arg(algorithmName(*algorithm));
    if (!cnonce.isEmpty())
        parts << QStringLiteral("cnonce=%1").arg(sip::quote(cnonce));
    if (!challenge.opaque.isEmpty())
        parts << QStringLiteral("opaque=%1").arg(sip::quote(challenge.opaque));
    if (!qop.isEmpty())
        parts << QStringLiteral("qop=%1").arg(qop) << QStringLiteral("nc=%1").arg(nc);
    return QStringLiteral("Digest ") + parts.join(QStringLiteral(", "));
}

} // namespace faxwerk
