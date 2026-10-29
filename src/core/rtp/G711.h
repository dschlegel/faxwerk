#pragma once

#include <cstddef>
#include <cstdint>

namespace faxwerk::g711 {

// ITU-T G.711 companding (A-law and µ-law), sample-by-sample.
std::uint8_t linearToAlaw(std::int16_t pcm);
std::int16_t alawToLinear(std::uint8_t alaw);
std::uint8_t linearToUlaw(std::int16_t pcm);
std::int16_t ulawToLinear(std::uint8_t ulaw);

// Buffer helpers; payloadType is the RTP payload type (0 = PCMU, 8 = PCMA).
void encode(int payloadType, const std::int16_t *pcm, std::uint8_t *out, std::size_t samples);
void decode(int payloadType, const std::uint8_t *in, std::int16_t *pcm, std::size_t samples);

} // namespace faxwerk::g711
