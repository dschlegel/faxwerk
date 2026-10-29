#pragma once

#include "document/FaxTiff.h"

#include <QImage>
#include <QList>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QString>

namespace faxwerk::test {

// Writes an A4 PDF with `pages` pages. If landscapeLast is set, the last page
// is in landscape orientation.
inline bool makeTestPdf(const QString &path, int pages, bool landscapeLast = false)
{
    QPdfWriter writer(path);
    writer.setResolution(300);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter painter;
    for (int i = 0; i < pages; ++i) {
        if (i == pages - 1 && landscapeLast)
            writer.setPageOrientation(QPageLayout::Landscape);
        if (i == 0) {
            if (!painter.begin(&writer))
                return false;
        } else if (!writer.newPage()) {
            return false;
        }
        const QRect area = painter.viewport();
        painter.fillRect(area, Qt::white);
        painter.setPen(QPen(Qt::black, 12));
        painter.drawRect(area.adjusted(60, 60, -60, -60));
        QFont font = painter.font();
        font.setPixelSize(120);
        painter.setFont(font);
        painter.drawText(area.adjusted(150, 150, -150, -150), Qt::AlignTop | Qt::AlignLeft,
                         QStringLiteral("Test page %1 of %2").arg(i + 1).arg(pages));
        for (int bar = 0; bar < 5; ++bar)
            painter.fillRect(QRect(200, 600 + bar * 200, 400 + bar * 300, 60), Qt::black);
    }
    painter.end();
    return true;
}

// Simple 1 bit fax pages (mostly white so the transfer stays short).
inline QList<QImage> makeTestFaxPages(int pages, int height = 1100)
{
    QList<QImage> result;
    for (int i = 0; i < pages; ++i) {
        QImage page(faxpage::kWidth, height, QImage::Format_Mono);
        page.setColorTable({qRgb(255, 255, 255), qRgb(0, 0, 0)});
        page.fill(0);
        QPainter painter(&page);
        painter.setPen(Qt::color1);
        painter.drawRect(20, 20, faxpage::kWidth - 41, height - 41);
        for (int bar = 0; bar <= i; ++bar)
            painter.fillRect(QRect(100, 100 + bar * 80, 600, 40), Qt::color1);
        painter.end();
        result.append(page);
    }
    return result;
}

} // namespace faxwerk::test
