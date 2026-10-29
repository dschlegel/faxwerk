#include "RtpSession.h"

#include "G711.h"

#include <QNetworkDatagram>
#include <QRandomGenerator>

#include <algorithm>
#include <vector>

namespace faxwerk {

namespace {

constexpr int kRtpHeaderSize = 12;
// Bursts larger than this are not sent; the schedule is reset instead
// (e.g. after the process was suspended).
constexpr quint64 kMaxBurstPackets = 25;

void putBe16(char *p, quint16 v)
{
    p[0] = char(v >> 8);
    p[1] = char(v);
}

void putBe32(char *p, quint32 v)
{
    p[0] = char(v >> 24);
    p[1] = char(v >> 16);
    p[2] = char(v >> 8);
    p[3] = char(v);
}

quint16 getBe16(const uchar *p)
{
    return quint16((p[0] << 8) | p[1]);
}

quint32 getBe32(const uchar *p)
{
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

} // namespace

RtpSession::RtpSession(QObject *parent)
    : RtpSession(Options{}, parent)
{
}

RtpSession::RtpSession(const Options &options, QObject *parent)
    : QObject(parent)
    , m_options(options)
{
    m_ssrc = QRandomGenerator::global()->generate();
    m_txSequence = quint16(QRandomGenerator::global()->bounded(65536));
    m_txTimestamp = QRandomGenerator::global()->generate();

    m_timer.setTimerType(Qt::PreciseTimer);
    const auto intervalMs = std::chrono::duration_cast<std::chrono::milliseconds>(m_options.packetInterval).count();
    // Tick a few times per packet interval to keep send jitter low.
    m_timer.setInterval(int(std::clamp<long long>(intervalMs / 4, 1, 5)));
    connect(&m_timer, &QTimer::timeout, this, &RtpSession::onTick);
    connect(&m_socket, &QUdpSocket::readyRead, this, &RtpSession::onReadyRead);
}

RtpSession::~RtpSession() = default;

bool RtpSession::open(const QHostAddress &bindAddress, quint16 port, QString *error)
{
    if (port != 0) {
        if (m_socket.bind(bindAddress, port))
            return true;
    } else {
        // RFC 3550 recommends an even RTP port.
        for (int attempt = 0; attempt < 32; ++attempt) {
            const quint16 candidate = quint16(40000 + 2 * QRandomGenerator::global()->bounded(5000));
            if (m_socket.bind(bindAddress, candidate))
                return true;
        }
        if (m_socket.bind(bindAddress, 0))
            return true;
    }
    if (error)
        *error = m_socket.errorString();
    return false;
}

quint16 RtpSession::localPort() const
{
    return m_socket.localPort();
}

void RtpSession::setRemote(const QHostAddress &address, quint16 port, int payloadType)
{
    m_remoteAddress = address;
    m_remotePort = port;
    m_payloadType = payloadType;
}

void RtpSession::start()
{
    if (m_timer.isActive())
        return;
    m_clock.start();
    m_packetsScheduled = 0;
    m_marker = true;
    m_timer.start();
    onTick();
}

void RtpSession::stop()
{
    m_timer.stop();
}

void RtpSession::onTick()
{
    const auto interval = std::chrono::duration_cast<std::chrono::nanoseconds>(m_options.packetInterval).count();
    const quint64 due = quint64(m_clock.nsecsElapsed() / interval) + 1;
    if (due > m_packetsScheduled + kMaxBurstPackets) {
        // Far behind schedule: restart the schedule instead of flooding.
        m_clock.restart();
        m_packetsScheduled = 0;
        sendPacket();
        ++m_packetsScheduled;
        return;
    }
    while (m_packetsScheduled < due) {
        sendPacket();
        ++m_packetsScheduled;
    }
}

void RtpSession::sendPacket()
{
    const int samples = m_options.samplesPerPacket;
    std::vector<std::int16_t> pcm(size_t(samples), 0);
    if (m_audio)
        m_audio->produceAudio(pcm.data(), samples);

    QByteArray packet(kRtpHeaderSize + samples, Qt::Uninitialized);
    char *p = packet.data();
    p[0] = char(0x80); // V=2, no padding, no extension, no CSRC
    p[1] = char((m_marker ? 0x80 : 0x00) | (m_payloadType & 0x7F));
    putBe16(p + 2, m_txSequence);
    putBe32(p + 4, m_txTimestamp);
    putBe32(p + 8, m_ssrc);
    g711::encode(m_payloadType, pcm.data(), reinterpret_cast<std::uint8_t *>(p + kRtpHeaderSize), size_t(samples));

    m_marker = false;
    ++m_txSequence;
    m_txTimestamp += quint32(samples);

    if (m_remotePort == 0 || m_remoteAddress.isNull())
        return;
    if (m_socket.writeDatagram(packet, m_remoteAddress, m_remotePort) < 0)
        emit socketError(m_socket.errorString());
    else
        ++m_stats.packetsSent;
}

void RtpSession::onReadyRead()
{
    while (m_socket.hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_socket.receiveDatagram();
        if (datagram.isValid())
            handleDatagram(datagram.data());
    }
}

void RtpSession::handleDatagram(const QByteArray &data)
{
    if (data.size() < kRtpHeaderSize) {
        ++m_stats.packetsDropped;
        return;
    }
    const auto *p = reinterpret_cast<const uchar *>(data.constData());
    if ((p[0] >> 6) != 2) {
        ++m_stats.packetsDropped;
        return;
    }
    const bool padding = p[0] & 0x20;
    const bool extension = p[0] & 0x10;
    const int csrcCount = p[0] & 0x0F;
    const int payloadType = p[1] & 0x7F;
    const quint16 sequence = getBe16(p + 2);
    const quint32 ssrc = getBe32(p + 8);

    int offset = kRtpHeaderSize + 4 * csrcCount;
    int end = data.size();
    if (extension) {
        if (offset + 4 > end) {
            ++m_stats.packetsDropped;
            return;
        }
        offset += 4 + 4 * getBe16(p + offset + 2);
    }
    if (padding && end > offset)
        end -= p[end - 1];
    if (offset > end) {
        ++m_stats.packetsDropped;
        return;
    }
    if (payloadType != 0 && payloadType != 8) {
        // Comfort noise, telephone-event, ...: not audio we can feed to the modem.
        ++m_stats.packetsDropped;
        return;
    }

    if (!m_rxActive || ssrc != m_rxSsrc) {
        // First packet or the sender restarted its stream (SSRC change):
        // resynchronize on this packet.
        if (m_rxActive)
            ++m_stats.ssrcChanges;
        m_rxActive = true;
        m_rxSsrc = ssrc;
        m_rxExpectedSequence = sequence;
        if (m_stats.packetsReceived == 0)
            emit firstPacketReceived();
    }

    const qint16 delta = qint16(quint16(sequence - m_rxExpectedSequence));
    if (delta < 0) {
        // Late or duplicated packet: the gap has already been concealed.
        ++m_stats.packetsDropped;
        return;
    }
    if (delta > m_options.maxConcealedPackets) {
        // Huge jump: treat as a new stream rather than concealing seconds of audio.
        m_rxExpectedSequence = sequence;
    } else if (delta > 0) {
        m_stats.packetsLost += quint64(delta);
        if (m_audio)
            m_audio->receiveGap(delta * m_options.samplesPerPacket);
    }
    m_rxExpectedSequence = quint16(sequence + 1);
    ++m_stats.packetsReceived;

    const int samples = end - offset;
    if (samples <= 0 || !m_audio)
        return;
    std::vector<std::int16_t> pcm(static_cast<size_t>(samples));
    g711::decode(payloadType, p + offset, pcm.data(), size_t(samples));
    m_audio->receiveAudio(pcm.data(), samples);
}

} // namespace faxwerk
