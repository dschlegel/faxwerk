#include "G711.h"

namespace faxwerk::g711 {

namespace {

constexpr int kUlawBias = 0x84;
constexpr int kUlawClip = 32635;

int topBit(unsigned int value)
{
    int bit = -1;
    while (value) {
        value >>= 1;
        ++bit;
    }
    return bit;
}

} // namespace

std::uint8_t linearToAlaw(std::int16_t pcm)
{
    int linear = pcm;
    int mask;
    if (linear >= 0) {
        mask = 0xD5; // sign bit set, even bits inverted
    } else {
        mask = 0x55;
        linear = -linear - 1;
    }
    // Segment from the position of the top bit of the 13 bit magnitude.
    const int seg = topBit(unsigned(linear | 0xFF)) - 7;
    int alaw;
    if (seg >= 8)
        alaw = 0x7F;
    else
        alaw = (seg << 4) | ((linear >> ((seg) ? (seg + 3) : 4)) & 0x0F);
    return std::uint8_t(alaw ^ mask);
}

std::int16_t alawToLinear(std::uint8_t alaw)
{
    const int a = alaw ^ 0x55;
    int i = ((a & 0x0F) << 4);
    const int seg = (a & 0x70) >> 4;
    if (seg)
        i = (i + 0x108) << (seg - 1);
    else
        i += 8;
    return std::int16_t((a & 0x80) ? i : -i);
}

std::uint8_t linearToUlaw(std::int16_t pcm)
{
    int linear = pcm;
    int mask;
    if (linear < 0) {
        linear = kUlawBias - linear - 1;
        mask = 0x7F;
    } else {
        linear = kUlawBias + linear;
        mask = 0xFF;
    }
    if (linear > kUlawClip + kUlawBias)
        linear = kUlawClip + kUlawBias;
    const int seg = topBit(unsigned(linear | 0xFF)) - 7;
    int ulaw;
    if (seg >= 8)
        ulaw = 0x7F;
    else
        ulaw = (seg << 4) | ((linear >> (seg + 3)) & 0x0F);
    return std::uint8_t(ulaw ^ mask);
}

std::int16_t ulawToLinear(std::uint8_t ulaw)
{
    const int u = ~ulaw & 0xFF;
    const int t = (((u & 0x0F) << 3) + kUlawBias) << ((u & 0x70) >> 4);
    return std::int16_t((u & 0x80) ? (kUlawBias - t) : (t - kUlawBias));
}

void encode(int payloadType, const std::int16_t *pcm, std::uint8_t *out, std::size_t samples)
{
    if (payloadType == 0) {
        for (std::size_t i = 0; i < samples; ++i)
            out[i] = linearToUlaw(pcm[i]);
    } else {
        for (std::size_t i = 0; i < samples; ++i)
            out[i] = linearToAlaw(pcm[i]);
    }
}

void decode(int payloadType, const std::uint8_t *in, std::int16_t *pcm, std::size_t samples)
{
    if (payloadType == 0) {
        for (std::size_t i = 0; i < samples; ++i)
            pcm[i] = ulawToLinear(in[i]);
    } else {
        for (std::size_t i = 0; i < samples; ++i)
            pcm[i] = alawToLinear(in[i]);
    }
}

} // namespace faxwerk::g711
