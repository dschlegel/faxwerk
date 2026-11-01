#include "fax/HeaderText.h"

#include <QtTest>

using namespace faxwerk;

class TestHeaderText : public QObject
{
    Q_OBJECT

private slots:
    void encodes_data();
    void encodes();
};

void TestHeaderText::encodes_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QByteArray>("expected");

    QTest::newRow("ascii") << QStringLiteral("Praxis SONARA +49 6155")
                           << QByteArrayLiteral("Praxis SONARA +49 6155");
    // The bug report: "ä" was sent as Latin-1 0xE4, which the font draws as "Σ".
    QTest::newRow("umlaut") << QStringLiteral("Logopädische Praxis")
                            << QByteArrayLiteral("Logop\x84" "dische Praxis");
    QTest::newRow("german") << QStringLiteral("ÄÖÜäöüß")
                            << QByteArrayLiteral("\x8E\x99\x9A\x84\x94\x81\xE1");
    QTest::newRow("decomposed") << (QStringLiteral("a") + QChar(0x0308))
                                << QByteArrayLiteral("\x84");
    QTest::newRow("unmappable") << QStringLiteral("5 €") << QByteArrayLiteral("5 ?");
    QTest::newRow("control") << QStringLiteral("a\tb") << QByteArrayLiteral("a b");
}

void TestHeaderText::encodes()
{
    QFETCH(QString, text);
    QFETCH(QByteArray, expected);
    QCOMPARE(toFaxHeaderCharset(text), expected);
}

QTEST_GUILESS_MAIN(TestHeaderText)
#include "tst_headertext.moc"
