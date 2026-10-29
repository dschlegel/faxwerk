#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

namespace faxwerk {

// RTP payload types used for fax over G.711.
enum : int {
    kPayloadPcmu = 0,
    kPayloadPcma = 8,
};

struct SdpMedia
{
    QString type;           // "audio", "image", ...
    quint16 port = 0;
    QString protocol;       // "RTP/AVP", "udptl", ...
    QStringList formats;    // payload types or "t38"
    QString connection;     // media level c= address (empty = use session level)
    QMap<int, QString> rtpmaps; // payload type -> "PCMA/8000"
    QStringList attributes; // all a= lines without "a="
    QString direction;      // sendrecv, sendonly, recvonly, inactive (empty = default)
    int ptime = 0;

    bool isT38() const;
    bool isAudio() const { return type == QLatin1String("audio"); }
};

struct SdpSession
{
    QString originUser = QStringLiteral("-");
    quint64 sessionId = 0;
    quint64 sessionVersion = 0;
    QString originAddress;
    QString sessionName = QStringLiteral("-");
    QString connection;     // session level c= address
    QString direction;      // session level direction attribute
    QList<SdpMedia> media;

    static std::optional<SdpSession> parse(const QByteArray &data);
    QByteArray toByteArray() const;

    // Connection address for a media stream (media level overrides session level).
    QString connectionFor(const SdpMedia &m) const;
    bool offersT38() const;
    // First audio stream with a non-zero port, if any.
    const SdpMedia *activeAudio() const;
};

// The negotiated remote end of the audio stream.
struct AudioEndpointInfo
{
    QHostAddress address;
    quint16 port = 0;
    int payloadType = kPayloadPcma;
    bool onHold = false; // remote sent inactive/sendonly or 0.0.0.0
};

namespace sdp {

// Our offer: G.711 A-law preferred, then µ-law, no telephone-event, ptime 20, sendrecv.
SdpSession makeOffer(const QHostAddress &localAddress, quint16 rtpPort, quint64 sessionId,
                     quint64 version);

// Our answer to a remote offer: picks the first codec from our preference list
// (PCMA, then PCMU) that the remote offers. Returns std::nullopt if the offer
// contains no usable audio stream (e.g. T.38 only).
std::optional<SdpSession> makeAnswer(const SdpSession &offer, const QHostAddress &localAddress,
                                     quint16 rtpPort, quint64 sessionId, quint64 version,
                                     int *chosenPayloadType = nullptr);

// Extracts the remote audio endpoint from an answer (or an offer we accepted).
// The payload type is the first G.711 type in the remote's list.
std::optional<AudioEndpointInfo> remoteAudio(const SdpSession &session);

} // namespace sdp
} // namespace faxwerk
