#include "PdfRasterizer.h"

#include "FaxTiff.h"

#include <QFileInfo>
#include <QPdfDocument>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <vector>

namespace faxwerk {

namespace {

QString pdfErrorText(QPdfDocument::Error error)
{
    switch (error) {
    case QPdfDocument::Error::None: return QString();
    case QPdfDocument::Error::FileNotFound: return QStringLiteral("file not found");
    case QPdfDocument::Error::InvalidFileFormat: return QStringLiteral("not a valid PDF file");
    case QPdfDocument::Error::IncorrectPassword: return QStringLiteral("PDF is password protected");
    case QPdfDocument::Error::UnsupportedSecurityScheme: return QStringLiteral("unsupported PDF security scheme");
    default: return QStringLiteral("unknown PDF error");
    }
}

// Composites a premultiplied ARGB rendering over white and converts to grey.
QImage toGreyOnWhite(const QImage &rendered)
{
    const QImage src = rendered.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage grey(src.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < src.height(); ++y) {
        const auto *in = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        uchar *out = grey.scanLine(y);
        for (int x = 0; x < src.width(); ++x) {
            const QRgb p = in[x];
            const int white = 255 - qAlpha(p);
            const int r = qRed(p) + white;
            const int g = qGreen(p) + white;
            const int b = qBlue(p) + white;
            out[x] = uchar((r * 299 + g * 587 + b * 114) / 1000);
        }
    }
    return grey;
}

} // namespace

bool PdfRasterizer::rasterize(const QStringList &pdfFiles, QList<QImage> *pages, QString *error) const
{
    QList<QImage> result;
    for (const QString &file : pdfFiles) {
        QPdfDocument document(nullptr);
        const QPdfDocument::Error loadError = document.load(file);
        if (loadError != QPdfDocument::Error::None) {
            if (error)
                *error = QStringLiteral("%1: %2").arg(QFileInfo(file).fileName(), pdfErrorText(loadError));
            return false;
        }
        if (document.pageCount() <= 0) {
            if (error)
                *error = QStringLiteral("%1: document has no pages").arg(QFileInfo(file).fileName());
            return false;
        }
        for (int page = 0; page < document.pageCount(); ++page) {
            QImage image = renderPage(document, page);
            if (image.isNull()) {
                if (error)
                    *error = QStringLiteral("%1: rendering page %2 failed").arg(QFileInfo(file).fileName()).arg(page + 1);
                return false;
            }
            result.append(image);
        }
    }
    if (pages)
        *pages = result;
    return true;
}

int PdfRasterizer::rasterizeToTiff(const QStringList &pdfFiles, const QString &tiffPath,
                                   const QString &software, QString *error) const
{
    QList<QImage> pages;
    if (!rasterize(pdfFiles, &pages, error))
        return -1;
    if (!writeFaxTiff(tiffPath, pages, software, error))
        return -1;
    return int(pages.size());
}

QImage PdfRasterizer::renderPage(QPdfDocument &document, int page) const
{
    const QSizeF points = document.pagePointSize(page);
    if (points.width() <= 0 || points.height() <= 0)
        return QImage();

    const bool landscape = points.width() > points.height();
    // Page size in inches, in fax orientation (portrait).
    const double faxWidthIn = (landscape ? points.height() : points.width()) / 72.0;
    const double faxHeightIn = (landscape ? points.width() : points.height()) / 72.0;
    const double xPixels = faxWidthIn * faxpage::kXDpi;
    const double yPixels = faxHeightIn * faxpage::kYDpi;
    const double scale = std::min({1.0, faxpage::kWidth / xPixels, faxpage::kMaxRowsA4 / yPixels});
    const int width = std::max(1, int(std::lround(xPixels * scale)));
    const int height = std::max(1, int(std::lround(yPixels * scale)));

    // QtPdf scales the page non-uniformly to the requested size, which gives
    // us the anisotropic 204x196 dpi raster directly. A landscape page is
    // rendered with swapped axes and rotated afterwards.
    const QSize renderSize = landscape ? QSize(height, width) : QSize(width, height);
    QImage rendered = document.render(page, renderSize);
    if (rendered.isNull())
        return QImage();
    if (landscape)
        rendered = rendered.transformed(QTransform().rotate(90));

    return toFaxBitmap(toGreyOnWhite(rendered));
}

QImage PdfRasterizer::toFaxBitmap(const QImage &greyInput) const
{
    const QImage grey = greyInput.convertToFormat(QImage::Format_Grayscale8);
    const int srcWidth = std::min(grey.width(), faxpage::kWidth);
    const int height = std::min(grey.height(), faxpage::kMaxRowsA4);
    const int xOffset = (faxpage::kWidth - srcWidth) / 2;

    QImage mono(faxpage::kWidth, height, QImage::Format_Mono);
    mono.setColorTable({qRgb(255, 255, 255), qRgb(0, 0, 0)});
    mono.fill(0);
    mono.setDotsPerMeterX(int(std::lround(faxpage::kXDpi / 0.0254)));
    mono.setDotsPerMeterY(int(std::lround(faxpage::kYDpi / 0.0254)));

    auto setBlack = [&mono](int x, int y) {
        mono.scanLine(y)[x >> 3] |= uchar(0x80 >> (x & 7));
    };

    if (m_options.mode == Mode::Threshold) {
        for (int y = 0; y < height; ++y) {
            const uchar *in = grey.constScanLine(y);
            for (int x = 0; x < srcWidth; ++x) {
                if (in[x] < m_options.threshold)
                    setBlack(x + xOffset, y);
            }
        }
        return mono;
    }

    // Floyd-Steinberg error diffusion over two rolling error rows.
    std::vector<float> current(size_t(srcWidth + 2), 0.0f);
    std::vector<float> next(size_t(srcWidth + 2), 0.0f);
    for (int y = 0; y < height; ++y) {
        const uchar *in = grey.constScanLine(y);
        std::fill(next.begin(), next.end(), 0.0f);
        for (int x = 0; x < srcWidth; ++x) {
            const float value = float(in[x]) + current[size_t(x + 1)];
            const bool black = value < 128.0f;
            const float err = value - (black ? 0.0f : 255.0f);
            if (black)
                setBlack(x + xOffset, y);
            current[size_t(x + 2)] += err * 7.0f / 16.0f;
            next[size_t(x)] += err * 3.0f / 16.0f;
            next[size_t(x + 1)] += err * 5.0f / 16.0f;
            next[size_t(x + 2)] += err * 1.0f / 16.0f;
        }
        std::swap(current, next);
    }
    return mono;
}

} // namespace faxwerk
