#pragma once

#include <cstdint>

namespace faxwerk {

// Connects a media transport (e.g. RtpSession) to an audio processor such as
// the fax modem. Samples are 16 bit linear PCM at 8 kHz.
class AudioPort
{
public:
    virtual ~AudioPort() = default;

    // Audio received from the remote side.
    virtual void receiveAudio(const std::int16_t *samples, int count) = 0;
    // `count` samples are missing (packet loss) and should be concealed.
    virtual void receiveGap(int count) = 0;
    // Fill `samples` with `count` samples to transmit.
    virtual void produceAudio(std::int16_t *samples, int count) = 0;
};

} // namespace faxwerk
