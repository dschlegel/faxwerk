#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

class QPdfDocument;

namespace faxwerk {

// Renders PDF pages into fax resolution bitmaps (QtPdf).
//
// Every page is rendered at 204x196 dpi, landscape pages are rotated to
// portrait, the result is centered on a 1728 pixel wide line and scaled down
// (keeping the aspect ratio) if it would be wider than a fax line or longer
// than A4.
class PdfRasterizer
{
public:
    enum class Mode {
        Threshold, // crisp text, default
        Dither,    // Floyd-Steinberg error diffusion, for photos/greyscale
    };

    struct Options
    {
        Mode mode = Mode::Threshold;
        int threshold = 160; // grey values below are black (0..255)
    };

    PdfRasterizer() = default;
    explicit PdfRasterizer(const Options &options) : m_options(options) {}

    // Renders all pages of all documents, in order.
    bool rasterize(const QStringList &pdfFiles, QList<QImage> *pages, QString *error = nullptr) const;

    // Rasterizes and writes a fax TIFF. Returns the number of pages or -1.
    int rasterizeToTiff(const QStringList &pdfFiles, const QString &tiffPath, const QString &software,
                        QString *error = nullptr) const;

    QImage renderPage(QPdfDocument &document, int page) const;

    // Converts a greyscale image (Format_Grayscale8) to a 1728 pixel wide Format_Mono page.
    QImage toFaxBitmap(const QImage &grey) const;

private:
    Options m_options;
};

} // namespace faxwerk
