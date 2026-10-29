#pragma once

#include "AudioPort.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>

#include <chrono>

namespace faxwerk {

// A minimal RTP (RFC 3550) endpoint for G.711 audio.
//
// Transmission is paced drift-free against a QElapsedTimer: on every timer
// tick all packets that are due by now are sent, so timer jitter does not
// accumulate. The packet interval is configurable so tests can run faster
// than real time; each packet always carries `samplesPerPacket` samples.
class RtpSession : public QObject
{
    Q_OBJECT

public:
    struct Options
    {
        std::chrono::microseconds packetInterval{20000};
        int samplesPerPacket = 160;
        // Gaps larger than this (in packets) are treated as a stream reset
        // instead of being concealed.
        int maxConcealedPackets = 50;
    };

    struct Statistics
    {
        quint64 packetsSent = 0;
        quint64 packetsReceived = 0;
        quint64 packetsLost = 0;    // concealed via AudioPort::receiveGap()
        quint64 packetsDropped = 0; // late, duplicate, malformed or foreign payload
        int ssrcChanges = 0;
    };

    explicit RtpSession(QObject *parent = nullptr);
    RtpSession(const Options &options, QObject *parent = nullptr);
    ~RtpSession() override;

    // Binds the local socket. With port 0 an even port from the dynamic range
    // is chosen.
    bool open(const QHostAddress &bindAddress, quint16 port = 0, QString *error = nullptr);
    quint16 localPort() const;

    void setRemote(const QHostAddress &address, quint16 port, int payloadType);
    void setAudioPort(AudioPort *port) { m_audio = port; }

    void start();
    void stop();
    bool isRunning() const { return m_timer.isActive(); }

    const Statistics &statistics() const { return m_stats; }
    const Options &options() const { return m_options; }

signals:
    void firstPacketReceived();
    void socketError(const QString &message);

private:
    void onTick();
    void sendPacket();
    void onReadyRead();
    void handleDatagram(const QByteArray &data);

    Options m_options;
    QUdpSocket m_socket;
    QTimer m_timer;
    QElapsedTimer m_clock;
    AudioPort *m_audio = nullptr;

    QHostAddress m_remoteAddress;
    quint16 m_remotePort = 0;
    int m_payloadType = 8;

    // Transmit state
    quint32 m_ssrc = 0;
    quint16 m_txSequence = 0;
    quint32 m_txTimestamp = 0;
    quint64 m_packetsScheduled = 0; // packets sent since m_clock was (re)started
    bool m_marker = true;

    // Receive state
    bool m_rxActive = false;
    quint32 m_rxSsrc = 0;
    quint16 m_rxExpectedSequence = 0;

    Statistics m_stats;
};

} // namespace faxwerk
