#include "Sdp.h"

namespace faxwerk {

namespace {

bool isDirection(const QString &attr)
{
    return attr == QLatin1String("sendrecv") || attr == QLatin1String("sendonly")
           || attr == QLatin1String("recvonly") || attr == QLatin1String("inactive");
}

QString addressFromConnectionLine(const QString &value)
{
    // c=IN IP4 192.168.178.1
    const QStringList parts = value.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() < 3)
        return QString();
    // Strip a multicast TTL suffix ("/127") if present.
    return parts.at(2).section(QLatin1Char('/'), 0, 0);
}

QString connectionLine(const QHostAddress &address)
{
    const bool v6 = address.protocol() == QAbstractSocket::IPv6Protocol;
    return QStringLiteral("IN %1 %2").arg(v6 ? QStringLiteral("IP6") : QStringLiteral("IP4"),
                                          address.toString());
}

bool isG711(int pt)
{
    return pt == kPayloadPcma || pt == kPayloadPcmu;
}

SdpSession baseSession(const QHostAddress &localAddress, quint64 sessionId, quint64 version)
{
    SdpSession s;
    s.sessionId = sessionId;
    s.sessionVersion = version;
    s.originAddress = localAddress.toString();
    s.connection = localAddress.toString();
    return s;
}

SdpMedia audioMedia(quint16 rtpPort, const QList<int> &payloadTypes)
{
    SdpMedia m;
    m.type = QStringLiteral("audio");
    m.port = rtpPort;
    m.protocol = QStringLiteral("RTP/AVP");
    for (int pt : payloadTypes) {
        m.formats.append(QString::number(pt));
        m.rtpmaps.insert(pt, pt == kPayloadPcma ? QStringLiteral("PCMA/8000") : QStringLiteral("PCMU/8000"));
    }
    m.ptime = 20;
    m.direction = QStringLiteral("sendrecv");
    return m;
}

} // namespace

bool SdpMedia::isT38() const
{
    return type == QLatin1String("image") && port != 0
           && protocol.contains(QLatin1String("udptl"), Qt::CaseInsensitive)
           && formats.contains(QLatin1String("t38"), Qt::CaseInsensitive);
}

std::optional<SdpSession> SdpSession::parse(const QByteArray &data)
{
    SdpSession session;
    SdpMedia *current = nullptr;
    bool sawVersion = false;

    const QList<QByteArray> lines = data.split('\n');
    for (QByteArray rawLine : lines) {
        if (rawLine.endsWith('\r'))
            rawLine.chop(1);
        const QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.size() < 2 || line.at(1) != QLatin1Char('='))
            continue;
        const QChar type = line.at(0);
        const QString value = line.mid(2).trimmed();

        switch (type.toLatin1()) {
        case 'v':
            sawVersion = true;
            break;
        case 'o': {
            const QStringList p = value.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (p.size() >= 6) {
                session.originUser = p.at(0);
                session.sessionId = p.at(1).toULongLong();
                session.sessionVersion = p.at(2).toULongLong();
                session.originAddress = p.at(5);
            }
            break;
        }
        case 's':
            session.sessionName = value;
            break;
        case 'c':
            if (current)
                current->connection = addressFromConnectionLine(value);
            else
                session.connection = addressFromConnectionLine(value);
            break;
        case 'm': {
            // m=<media> <port>[/<count>] <proto> <fmt> ...
            const QStringList p = value.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (p.size() < 3)
                return std::nullopt;
            SdpMedia m;
            m.type = p.at(0).toLower();
            bool ok = false;
            m.port = quint16(p.at(1).section(QLatin1Char('/'), 0, 0).toUInt(&ok));
            if (!ok)
                return std::nullopt;
            m.protocol = p.at(2);
            m.formats = p.mid(3);
            session.media.append(m);
            current = &session.media.last();
            break;
        }
        case 'a': {
            const int colon = value.indexOf(QLatin1Char(':'));
            const QString name = colon < 0 ? value : value.left(colon);
            const QString arg = colon < 0 ? QString() : value.mid(colon + 1).trimmed();
            if (!current) {
                if (isDirection(name))
                    session.direction = name;
                break;
            }
            current->attributes.append(value);
            if (isDirection(name)) {
                current->direction = name;
            } else if (name == QLatin1String("rtpmap")) {
                const int sp = arg.indexOf(QLatin1Char(' '));
                if (sp > 0)
                    current->rtpmaps.insert(arg.left(sp).toInt(), arg.mid(sp + 1).trimmed());
            } else if (name == QLatin1String("ptime")) {
                current->ptime = arg.toInt();
            }
            break;
        }
        default:
            break;
        }
    }
    if (!sawVersion)
        return std::nullopt;
    return session;
}

QByteArray SdpSession::toByteArray() const
{
    QStringList lines;
    QHostAddress origin(originAddress);
    const bool v6 = origin.protocol() == QAbstractSocket::IPv6Protocol;
    lines << QStringLiteral("v=0")
          << QStringLiteral("o=%1 %2 %3 IN %4 %5")
                 .arg(originUser)
                 .arg(sessionId)
                 .arg(sessionVersion)
                 .arg(v6 ? QStringLiteral("IP6") : QStringLiteral("IP4"), originAddress)
          << QStringLiteral("s=%1").arg(sessionName);
    if (!connection.isEmpty())
        lines << QStringLiteral("c=%1").arg(connectionLine(QHostAddress(connection)));
    lines << QStringLiteral("t=0 0");
    if (!direction.isEmpty())
        lines << QStringLiteral("a=%1").arg(direction);
    for (const SdpMedia &m : media) {
        lines << QStringLiteral("m=%1 %2 %3 %4").arg(m.type).arg(m.port).arg(m.protocol, m.formats.join(QLatin1Char(' ')));
        if (!m.connection.isEmpty())
            lines << QStringLiteral("c=%1").arg(connectionLine(QHostAddress(m.connection)));
        bool wroteRtpmaps = false;
        for (const QString &attr : m.attributes) {
            lines << QStringLiteral("a=%1").arg(attr);
            wroteRtpmaps = wroteRtpmaps || attr.startsWith(QLatin1String("rtpmap:"));
        }
        if (m.attributes.isEmpty()) {
            for (auto it = m.rtpmaps.constBegin(); it != m.rtpmaps.constEnd(); ++it) {
                if (m.formats.contains(QString::number(it.key())))
                    lines << QStringLiteral("a=rtpmap:%1 %2").arg(it.key()).arg(it.value());
            }
            if (m.ptime > 0)
                lines << QStringLiteral("a=ptime:%1").arg(m.ptime);
            if (!m.direction.isEmpty())
                lines << QStringLiteral("a=%1").arg(m.direction);
        }
    }
    return lines.join(QStringLiteral("\r\n")).toUtf8() + "\r\n";
}

QString SdpSession::connectionFor(const SdpMedia &m) const
{
    return m.connection.isEmpty() ? connection : m.connection;
}

bool SdpSession::offersT38() const
{
    for (const SdpMedia &m : media) {
        if (m.isT38())
            return true;
    }
    return false;
}

const SdpMedia *SdpSession::activeAudio() const
{
    for (const SdpMedia &m : media) {
        if (m.isAudio() && m.port != 0)
            return &m;
    }
    return nullptr;
}

namespace sdp {

SdpSession makeOffer(const QHostAddress &localAddress, quint16 rtpPort, quint64 sessionId,
                     quint64 version)
{
    SdpSession s = baseSession(localAddress, sessionId, version);
    s.media.append(audioMedia(rtpPort, {kPayloadPcma, kPayloadPcmu}));
    return s;
}

std::optional<SdpSession> makeAnswer(const SdpSession &offer, const QHostAddress &localAddress,
                                     quint16 rtpPort, quint64 sessionId, quint64 version,
                                     int *chosenPayloadType)
{
    const SdpMedia *audio = offer.activeAudio();
    if (!audio || !audio->protocol.startsWith(QLatin1String("RTP/AVP"), Qt::CaseInsensitive))
        return std::nullopt;

    int chosen = -1;
    for (int preferred : {kPayloadPcma, kPayloadPcmu}) {
        if (audio->formats.contains(QString::number(preferred))) {
            chosen = preferred;
            break;
        }
    }
    if (chosen < 0)
        return std::nullopt;
    if (chosenPayloadType)
        *chosenPayloadType = chosen;

    SdpSession s = baseSession(localAddress, sessionId, version);
    // Answer every offered stream in order (RFC 3264): rejected ones with port 0.
    for (const SdpMedia &m : offer.media) {
        if (&m == audio) {
            s.media.append(audioMedia(rtpPort, {chosen}));
        } else {
            SdpMedia rejected;
            rejected.type = m.type;
            rejected.port = 0;
            rejected.protocol = m.protocol;
            rejected.formats = m.formats;
            s.media.append(rejected);
        }
    }
    return s;
}

std::optional<AudioEndpointInfo> remoteAudio(const SdpSession &session)
{
    const SdpMedia *audio = session.activeAudio();
    if (!audio)
        return std::nullopt;

    AudioEndpointInfo info;
    info.address = QHostAddress(session.connectionFor(*audio));
    info.port = audio->port;
    info.payloadType = -1;
    for (const QString &fmt : audio->formats) {
        bool ok = false;
        const int pt = fmt.toInt(&ok);
        if (ok && isG711(pt)) {
            info.payloadType = pt;
            break;
        }
    }
    if (info.payloadType < 0 || info.address.isNull())
        return std::nullopt;
    const QString direction = audio->direction.isEmpty() ? session.direction : audio->direction;
    info.onHold = direction == QLatin1String("inactive") || direction == QLatin1String("sendonly")
                  || info.address == QHostAddress(QHostAddress::AnyIPv4);
    return info;
}

} // namespace sdp
} // namespace faxwerk
