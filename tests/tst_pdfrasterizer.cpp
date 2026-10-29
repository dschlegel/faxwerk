#include "TestDocuments.h"

#include "document/FaxTiff.h"
#include "document/PdfRasterizer.h"

#include <QPdfDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace faxwerk;

class TestPdfRasterizer : public QObject
{
    Q_OBJECT

private slots:
    void threePagesToTiff_data();
    void threePagesToTiff();
    void pageGeometry();
    void reportsErrors();
};

namespace {

int blackPixels(const QImage &mono)
{
    int count = 0;
    for (int y = 0; y < mono.height(); ++y) {
        for (int x = 0; x < mono.width(); ++x)
            count += mono.pixelIndex(x, y);
    }
    return count;
}

} // namespace

void TestPdfRasterizer::threePagesToTiff_data()
{
    QTest::addColumn<bool>("dither");
    QTest::newRow("threshold") << false;
    QTest::newRow("dither") << true;
}

void TestPdfRasterizer::threePagesToTiff()
{
    QFETCH(bool, dither);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pdf = dir.filePath(QStringLiteral("doc.pdf"));
    const QString tiff = dir.filePath(QStringLiteral("doc.tif"));
    QVERIFY(test::makeTestPdf(pdf, 3));

    PdfRasterizer::Options options;
    options.mode = dither ? PdfRasterizer::Mode::Dither : PdfRasterizer::Mode::Threshold;
    QString error;
    QCOMPARE(PdfRasterizer(options).rasterizeToTiff({pdf}, tiff, QStringLiteral("test"), &error), 3);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    FaxTiffInfo info;
    QVERIFY(readFaxTiffInfo(tiff, &info, &error));
    QCOMPARE(info.pages, 3);
    for (int i = 0; i < info.pages; ++i) {
        QCOMPARE(info.widths.at(i), 1728);
        QCOMPARE(info.xResolutions.at(i), 204.0);
        QCOMPARE(info.yResolutions.at(i), 196.0);
        // A4 at 196 dpi: 2292 rows
        QVERIFY2(info.heights.at(i) > 2250 && info.heights.at(i) <= 2292, qPrintable(QString::number(info.heights.at(i))));
    }
}

void TestPdfRasterizer::pageGeometry()
{
    QTemporaryDir dir;
    const QString pdf = dir.filePath(QStringLiteral("mixed.pdf"));
    QVERIFY(test::makeTestPdf(pdf, 2, true));

    QPdfDocument document(nullptr);
    QCOMPARE(document.load(pdf), QPdfDocument::Error::None);
    QCOMPARE(document.pageCount(), 2);
    PdfRasterizer rasterizer;

    const QImage portrait = rasterizer.renderPage(document, 0);
    QCOMPARE(portrait.format(), QImage::Format_Mono);
    QCOMPARE(portrait.width(), 1728);
    QVERIFY(portrait.height() > 2250 && portrait.height() <= 2292);
    // A4 is 210 mm = 1654 px at 204 dpi: the page is centered with white margins.
    for (int y = 0; y < portrait.height(); y += 50) {
        QCOMPARE(portrait.pixelIndex(10, y), 0);
        QCOMPARE(portrait.pixelIndex(1717, y), 0);
    }
    QVERIFY(blackPixels(portrait) > 10000);
    // The frame is drawn about 5 mm inside the page edge (~37 px + margin).
    const int margin = (1728 - 1654) / 2;
    bool frameFound = false;
    for (int x = margin + 20; x < margin + 60; ++x)
        frameFound = frameFound || portrait.pixelIndex(x, portrait.height() / 2) == 1;
    QVERIFY(frameFound);

    // The landscape page is rotated to portrait and has the same geometry.
    const QImage landscape = rasterizer.renderPage(document, 1);
    QCOMPARE(landscape.width(), 1728);
    QVERIFY(landscape.height() > 2250 && landscape.height() <= 2292);
    QVERIFY(blackPixels(landscape) > 10000);
}

void TestPdfRasterizer::reportsErrors()
{
    QTemporaryDir dir;
    QString error;
    QList<QImage> pages;
    QVERIFY(!PdfRasterizer().rasterize({dir.filePath(QStringLiteral("missing.pdf"))}, &pages, &error));
    QVERIFY(!error.isEmpty());

    QFile junk(dir.filePath(QStringLiteral("junk.pdf")));
    QVERIFY(junk.open(QIODevice::WriteOnly));
    junk.write("this is not a pdf");
    junk.close();
    error.clear();
    QVERIFY(!PdfRasterizer().rasterize({junk.fileName()}, &pages, &error));
    QVERIFY(!error.isEmpty());

    QVERIFY(!writeFaxTiff(dir.filePath(QStringLiteral("x.tif")), {QImage(100, 100, QImage::Format_Mono)},
                          QString(), &error));
}

QTEST_MAIN(TestPdfRasterizer)
#include "tst_pdfrasterizer.moc"
