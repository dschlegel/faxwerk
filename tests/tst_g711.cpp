#include "rtp/G711.h"

#include <QtTest>

#include <cstdlib>

using namespace faxwerk;

class TestG711 : public QObject
{
    Q_OBJECT

private slots:
    void knownValues();
    void roundtrip_data();
    void roundtrip();
    void codesAreStable();
    void bufferHelpers();
};

void TestG711::knownValues()
{
    QCOMPARE(g711::linearToAlaw(0), std::uint8_t(0xD5));
    QCOMPARE(g711::linearToUlaw(0), std::uint8_t(0xFF));
    QCOMPARE(g711::alawToLinear(0xD5), std::int16_t(8));
    QCOMPARE(g711::ulawToLinear(0xFF), std::int16_t(0));
    QCOMPARE(g711::alawToLinear(0xAA), std::int16_t(32256));  // A-law maximum
    QCOMPARE(g711::alawToLinear(0x2A), std::int16_t(-32256));
    QCOMPARE(g711::ulawToLinear(0x80), std::int16_t(32124));  // µ-law maximum
    QCOMPARE(g711::ulawToLinear(0x00), std::int16_t(-32124));
}

void TestG711::roundtrip_data()
{
    QTest::addColumn<int>("payloadType");
    QTest::newRow("PCMA") << 8;
    QTest::newRow("PCMU") << 0;
}

void TestG711::roundtrip()
{
    QFETCH(int, payloadType);
    for (int value = -32768; value <= 32767; ++value) {
        const auto pcm = std::int16_t(value);
        std::uint8_t code = 0;
        std::int16_t decoded = 0;
        g711::encode(payloadType, &pcm, &code, 1);
        g711::decode(payloadType, &code, &decoded, 1);
        // Logarithmic quantization: the error grows with the magnitude
        // (max. half a step, 1/32 of the value in the top segments).
        const int error = std::abs(int(decoded) - value);
        const int tolerance = std::max(16, std::abs(value) / 16);
        if (error > tolerance)
            QFAIL(qPrintable(QStringLiteral("value %1 decoded as %2").arg(value).arg(decoded)));
        // Sign is preserved for anything that is not tiny.
        if (std::abs(value) > 64)
            QVERIFY((value < 0) == (decoded < 0));
    }
}

void TestG711::codesAreStable()
{
    // decode -> encode must reproduce the code word (except µ-law's negative zero).
    for (int code = 0; code < 256; ++code) {
        const auto c = std::uint8_t(code);
        QCOMPARE(g711::linearToAlaw(g711::alawToLinear(c)), c);
        if (code != 0x7F)
            QCOMPARE(g711::linearToUlaw(g711::ulawToLinear(c)), c);
    }
}

void TestG711::bufferHelpers()
{
    const std::int16_t pcm[4] = {0, 1000, -1000, 30000};
    std::uint8_t alaw[4];
    std::int16_t back[4];
    g711::encode(8, pcm, alaw, 4);
    g711::decode(8, alaw, back, 4);
    for (int i = 0; i < 4; ++i) {
        QCOMPARE(alaw[i], g711::linearToAlaw(pcm[i]));
        QVERIFY(std::abs(back[i] - pcm[i]) <= std::max(16, std::abs(pcm[i]) / 16));
    }
}

QTEST_GUILESS_MAIN(TestG711)
#include "tst_g711.moc"
