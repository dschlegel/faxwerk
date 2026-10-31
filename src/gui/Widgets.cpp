#include "Widgets.h"

#include "Theme.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace faxwerk::gui {

void paintLogo(QPainter &painter, const QRectF &rect)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(rect.topLeft());
    painter.scale(rect.width() / 32.0, rect.height() / 32.0);

    painter.setPen(Qt::NoPen);
    // The brand mark keeps its colors in every theme.
    painter.setBrush(lightColors().primary);
    painter.drawRoundedRect(QRectF(0, 0, 32, 32), 9, 9);

    QPen pen(Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    const QPointF wave[] = {{7, 16}, {10, 16}, {12, 10}, {15, 22}, {18, 13}, {20, 18}, {22, 16}, {25, 16}};
    painter.drawPolyline(wave, int(std::size(wave)));
    painter.restore();
}

QIcon appIcon()
{
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256, 512}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paintLogo(painter, QRectF(0, 0, size, size));
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

LogoMark::LogoMark(int size, QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(size, size);
}

void LogoMark::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    paintLogo(painter, rect());
}

// --- PageThumbnail -------------------------------------------------------------

PageThumbnail::PageThumbnail(const QImage &image, const QString &caption, QWidget *parent)
    : QWidget(parent)
    , m_image(image)
    , m_caption(caption)
{
    setFixedSize(kWidth + 16, kHeight + 36);
    setAccessibleName(caption);
}

QImage PageThumbnail::makeThumbnail(const QImage &faxPage)
{
    // Fax pages are 204x196 dpi: stretch vertically a little so the page has
    // its paper proportions, and go through greyscale for a smooth reduction.
    const QImage grey = faxPage.convertToFormat(QImage::Format_Grayscale8);
    const QSize target(kWidth * 2, int(kWidth * 2 * (double(grey.height()) * 204.0 / 196.0) / grey.width()));
    return grey.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

void PageThumbnail::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const QRectF sheet(8, 4, kWidth, kHeight);
    // Soft shadow below the sheet.
    for (int i = 6; i > 0; --i) {
        QColor shadow = isDarkTheme() ? QColor(Qt::black) : colors().deep;
        shadow.setAlphaF(0.025 * (7 - i));
        painter.setPen(Qt::NoPen);
        painter.setBrush(shadow);
        painter.drawRoundedRect(sheet.adjusted(-i * 0.5, i, i * 0.5, i), 6, 6);
    }
    // The sheet stays white in dark mode: it shows the paper as received.
    painter.setBrush(Qt::white);
    painter.setPen(QPen(colors().scroll, 1));
    painter.drawRoundedRect(sheet, 6, 6);

    if (!m_image.isNull()) {
        QPainterPath clip;
        clip.addRoundedRect(sheet.adjusted(1, 1, -1, -1), 5, 5);
        painter.setClipPath(clip);
        const QSizeF scaled = QSizeF(m_image.size()).scaled(sheet.size() - QSizeF(2, 2), Qt::KeepAspectRatio);
        const QRectF target(sheet.center().x() - scaled.width() / 2, sheet.top() + 1, scaled.width(),
                            scaled.height());
        painter.drawImage(target, m_image);
        painter.setClipping(false);
    }

    painter.setPen(colors().muted);
    QFont font = painter.font();
    font.setPixelSize(13);
    painter.setFont(font);
    painter.drawText(QRectF(0, sheet.bottom() + 8, width(), 22), Qt::AlignHCenter | Qt::AlignTop, m_caption);
}

// --- WaveformWidget -------------------------------------------------------------

WaveformWidget::WaveformWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(110);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_timer.setInterval(60);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        m_phase += 0.12;
        update();
    });
}

void WaveformWidget::setProgress(double progress)
{
    m_progress = std::clamp(progress, 0.0, 1.0);
    update();
}

void WaveformWidget::setActive(bool active)
{
    if (active)
        m_timer.start();
    else
        m_timer.stop();
    update();
}

void WaveformWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    constexpr double gap = 3;
    const int count = std::max(12, int(width() / 9));
    const double barWidth = (width() - gap * (count - 1)) / count;
    const double maxHeight = height() - 4;
    const int doneBars = int(std::round(m_progress * count));

    QColor ahead(Qt::white);
    ahead.setAlphaF(0.22);
    for (int i = 0; i < count; ++i) {
        double shape = std::abs(std::sin(i * 0.57) * std::cos(i * 0.21));
        if (i >= doneBars && m_timer.isActive())
            shape = shape * (0.75 + 0.25 * std::sin(m_phase + i * 0.4));
        const double h = std::max(8.0, (0.14 + 0.86 * shape) * maxHeight);
        const QRectF bar(i * (barWidth + gap), (height() - h) / 2, barWidth, h);
        painter.setBrush(i < doneBars ? lightColors().lilacSoft : ahead);
        painter.drawRoundedRect(bar, std::min(3.0, barWidth / 2), std::min(3.0, barWidth / 2));
    }
}

// --- PageProgressWidget -----------------------------------------------------------

PageProgressWidget::PageProgressWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void PageProgressWidget::setPages(int total, int done, bool sending)
{
    m_total = total;
    m_done = done;
    m_sending = sending;
    updateGeometry();
    update();
    setAccessibleName(QStringLiteral("%1 von %2 Seiten gesendet").arg(done).arg(total));
}

QSize PageProgressWidget::sizeHint() const
{
    const int shown = std::min(m_total, 10);
    return {std::max(0, shown * 44 + (shown - 1) * 10), 62};
}

void PageProgressWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const int shown = std::min(m_total, 10);
    for (int i = 0; i < shown; ++i) {
        const QRectF box(i * 54 + 0.75, 0.75, 42.5, 60.5);
        if (i < m_done) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(Qt::white);
            painter.drawRoundedRect(box, 5, 5);
            QPen check(lightColors().success, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter.setPen(check);
            const QPointF c = box.center();
            const QPointF mark[] = {{c.x() - 7, c.y()}, {c.x() - 2, c.y() + 5}, {c.x() + 7, c.y() - 5}};
            painter.drawPolyline(mark, 3);
        } else if (i == m_done && m_sending) {
            QColor faint(Qt::white);
            faint.setAlphaF(0.28);
            painter.setPen(Qt::NoPen);
            painter.setBrush(faint);
            painter.drawRoundedRect(box, 5, 5);
            painter.setBrush(Qt::white);
            painter.save();
            painter.setClipRect(QRectF(box.left(), box.top(), box.width(), box.height() * 0.5));
            painter.drawRoundedRect(box, 5, 5);
            painter.restore();
        } else {
            QColor outline(Qt::white);
            outline.setAlphaF(0.4);
            painter.setPen(QPen(outline, 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(box, 5, 5);
        }
    }
}

// --- StepList ---------------------------------------------------------------------

StepList::StepList(const QStringList &titles, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);
    for (const QString &title : titles) {
        Row row;
        auto *line = new QHBoxLayout;
        line->setSpacing(14);
        row.dot = new QLabel;
        row.dot->setFixedSize(28, 28);
        row.dot->setAlignment(Qt::AlignCenter);
        row.title = new QLabel(title);
        row.title->setWordWrap(true);
        row.detail = new QLabel;
        row.detail->setProperty("role", QStringLiteral("muted"));
        row.detail->setWordWrap(true);
        row.detail->hide();
        auto *text = new QVBoxLayout;
        text->setSpacing(2);
        text->addWidget(row.title);
        text->addWidget(row.detail);
        line->addWidget(row.dot, 0, Qt::AlignTop);
        line->addLayout(text, 1);
        layout->addLayout(line);
        m_rows.append(row);
    }
    reset();
}

void StepList::reset()
{
    for (int i = 0; i < m_rows.size(); ++i) {
        setState(i, State::Waiting);
        setDetail(i, QString());
    }
}

void StepList::setState(int index, State state)
{
    if (index < 0 || index >= m_rows.size())
        return;
    Row &row = m_rows[index];
    row.state = state;
    QString dotStyle;
    QString titleStyle;
    switch (state) {
    case State::Waiting:
        row.dot->setText(QString::number(index + 1));
        dotStyle = QStringLiteral("border: 1.5px solid @border; color: @muted;");
        titleStyle = QStringLiteral("color: @muted;");
        break;
    case State::Active:
        row.dot->setText(QString::number(index + 1));
        dotStyle = QStringLiteral("background: @primary; color: @onPrimary; border: 4px solid @lilac;");
        titleStyle = QStringLiteral("font-weight: 600; color: @text;");
        break;
    case State::Done:
        row.dot->setText(QStringLiteral("✓"));
        dotStyle = QStringLiteral("background: @lilac; color: @accentText;");
        titleStyle = QStringLiteral("font-weight: 500; color: @text;");
        break;
    case State::Failed:
        row.dot->setText(QStringLiteral("✕"));
        dotStyle = QStringLiteral("background: @failBg; color: @failText;");
        titleStyle = QStringLiteral("font-weight: 600; color: @failText;");
        break;
    }
    setThemedStyleSheet(row.dot, QStringLiteral("QLabel { border-radius: 14px; font-size: 13px; font-weight: 600; %1 }")
                                     .arg(dotStyle));
    setThemedStyleSheet(row.title, QStringLiteral("QLabel { font-size: 15px; %1 }").arg(titleStyle));
}

void StepList::setDetail(int index, const QString &detail)
{
    if (index < 0 || index >= m_rows.size())
        return;
    m_rows[index].detail->setText(detail);
    m_rows[index].detail->setVisible(!detail.isEmpty());
}

void setStyleProperty(QWidget *widget, const char *name, const QVariant &value)
{
    widget->setProperty(name, value);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

} // namespace faxwerk::gui
