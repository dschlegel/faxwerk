#include "FaxTiff.h"

#include <tiffio.h>

#include <memory>

namespace faxwerk {

namespace {

struct TiffCloser
{
    void operator()(TIFF *tif) const { TIFFClose(tif); }
};
using TiffPtr = std::unique_ptr<TIFF, TiffCloser>;

TIFF *openTiff(const QString &path, const char *mode)
{
#ifdef _WIN32
    return TIFFOpenW(reinterpret_cast<const wchar_t *>(path.utf16()), mode);
#else
    return TIFFOpen(path.toLocal8Bit().constData(), mode);
#endif
}

double toDpi(float value, uint16_t unit)
{
    return unit == RESUNIT_CENTIMETER ? double(value) * 2.54 : double(value);
}

} // namespace

bool writeFaxTiff(const QString &path, const QList<QImage> &pages, const QString &software,
                  QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (pages.isEmpty())
        return fail(QStringLiteral("no pages to write"));
    if (pages.size() > 0xFFFF)
        return fail(QStringLiteral("too many pages"));

    TiffPtr tif(openTiff(path, "w"));
    if (!tif)
        return fail(QStringLiteral("cannot create TIFF file %1").arg(path));

    const QByteArray softwareName = software.toUtf8();
    const auto pageCount = uint16_t(pages.size());
    for (int index = 0; index < pages.size(); ++index) {
        const QImage &page = pages.at(index);
        if (page.format() != QImage::Format_Mono || page.width() != faxpage::kWidth || page.height() <= 0)
            return fail(QStringLiteral("page %1 has an invalid format").arg(index + 1));

        TIFF *t = tif.get();
        TIFFSetField(t, TIFFTAG_SUBFILETYPE, FILETYPE_PAGE);
        TIFFSetField(t, TIFFTAG_IMAGEWIDTH, uint32_t(page.width()));
        TIFFSetField(t, TIFFTAG_IMAGELENGTH, uint32_t(page.height()));
        TIFFSetField(t, TIFFTAG_BITSPERSAMPLE, uint16_t(1));
        TIFFSetField(t, TIFFTAG_SAMPLESPERPIXEL, uint16_t(1));
        TIFFSetField(t, TIFFTAG_COMPRESSION, uint16_t(COMPRESSION_CCITTFAX3));
        TIFFSetField(t, TIFFTAG_GROUP3OPTIONS, uint32_t(0)); // 1-D modified Huffman
        TIFFSetField(t, TIFFTAG_PHOTOMETRIC, uint16_t(PHOTOMETRIC_MINISWHITE));
        TIFFSetField(t, TIFFTAG_FILLORDER, uint16_t(FILLORDER_LSB2MSB));
        TIFFSetField(t, TIFFTAG_PLANARCONFIG, uint16_t(PLANARCONFIG_CONTIG));
        TIFFSetField(t, TIFFTAG_ORIENTATION, uint16_t(ORIENTATION_TOPLEFT));
        TIFFSetField(t, TIFFTAG_XRESOLUTION, float(faxpage::kXDpi));
        TIFFSetField(t, TIFFTAG_YRESOLUTION, float(faxpage::kYDpi));
        TIFFSetField(t, TIFFTAG_RESOLUTIONUNIT, uint16_t(RESUNIT_INCH));
        TIFFSetField(t, TIFFTAG_ROWSPERSTRIP, uint32_t(page.height()));
        TIFFSetField(t, TIFFTAG_PAGENUMBER, uint16_t(index), pageCount);
        if (!softwareName.isEmpty())
            TIFFSetField(t, TIFFTAG_SOFTWARE, softwareName.constData());

        // Format_Mono stores pixels MSB first with index 1 = black, which is
        // exactly libtiff's in-memory layout for MINISWHITE.
        for (int y = 0; y < page.height(); ++y) {
            auto *line = const_cast<uchar *>(page.constScanLine(y));
            if (TIFFWriteScanline(t, line, uint32_t(y), 0) < 0)
                return fail(QStringLiteral("writing page %1 failed").arg(index + 1));
        }
        if (!TIFFWriteDirectory(t))
            return fail(QStringLiteral("writing page %1 failed").arg(index + 1));
    }
    tif.reset();
    return true;
}

bool readFaxTiffInfo(const QString &path, FaxTiffInfo *info, QString *error)
{
    TiffPtr tif(openTiff(path, "r"));
    if (!tif) {
        if (error)
            *error = QStringLiteral("cannot open TIFF file %1").arg(path);
        return false;
    }
    FaxTiffInfo result;
    do {
        uint32_t width = 0;
        uint32_t height = 0;
        float xres = 0;
        float yres = 0;
        uint16_t unit = RESUNIT_INCH;
        TIFFGetField(tif.get(), TIFFTAG_IMAGEWIDTH, &width);
        TIFFGetField(tif.get(), TIFFTAG_IMAGELENGTH, &height);
        TIFFGetField(tif.get(), TIFFTAG_XRESOLUTION, &xres);
        TIFFGetField(tif.get(), TIFFTAG_YRESOLUTION, &yres);
        TIFFGetFieldDefaulted(tif.get(), TIFFTAG_RESOLUTIONUNIT, &unit);
        ++result.pages;
        result.widths.append(int(width));
        result.heights.append(int(height));
        result.xResolutions.append(toDpi(xres, unit));
        result.yResolutions.append(toDpi(yres, unit));
    } while (TIFFReadDirectory(tif.get()));
    if (info)
        *info = result;
    return true;
}

} // namespace faxwerk
