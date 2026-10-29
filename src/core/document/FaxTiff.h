#pragma once

#include <QImage>
#include <QList>
#include <QString>

namespace faxwerk {

// Fax page geometry (ITU-T T.4, "fine" resolution).
namespace faxpage {
constexpr int kWidth = 1728;          // pixels per line
constexpr double kXDpi = 204.0;
constexpr double kYDpi = 196.0;
constexpr int kMaxRowsA4 = 2292;      // 297 mm at 196 dpi
} // namespace faxpage

// Writes 1 bit pages as a multi-page TIFF suitable for spandsp:
// CCITT G3 (T.4 1-D), MINISWHITE, 204x196 dpi, PAGENUMBER, one strip per page.
// Each image must be QImage::Format_Mono, 1728 pixels wide, with index 1 = black.
bool writeFaxTiff(const QString &path, const QList<QImage> &pages, const QString &software,
                  QString *error = nullptr);

struct FaxTiffInfo
{
    int pages = 0;
    QList<int> widths;
    QList<int> heights;
    QList<double> xResolutions; // dpi
    QList<double> yResolutions; // dpi
};

// Reads basic properties of every page of a TIFF file.
bool readFaxTiffInfo(const QString &path, FaxTiffInfo *info, QString *error = nullptr);

} // namespace faxwerk
