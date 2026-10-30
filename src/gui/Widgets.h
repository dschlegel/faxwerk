#pragma once

#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QList>
#include <QTimer>
#include <QWidget>

class QVBoxLayout;

namespace faxwerk::gui {

// The app mark: a rounded aubergine tile with a white sound wave.
void paintLogo(QPainter &painter, const QRectF &rect);
QIcon appIcon();

class LogoMark : public QWidget
{
public:
    explicit LogoMark(int size, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
};

// A sheet of paper showing one fax page as the recipient will get it.
class PageThumbnail : public QWidget
{
public:
    static constexpr int kWidth = 150;
    static constexpr int kHeight = 212;

    PageThumbnail(const QImage &image, const QString &caption, QWidget *parent = nullptr);
    static QImage makeThumbnail(const QImage &faxPage);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_image;
    QString m_caption;
};

// Bars of a sound wave; the part already sent is drawn bright. While active,
// the bars ahead of the progress move gently, so the window shows that the
// line is busy even between page events.
class WaveformWidget : public QWidget
{
public:
    explicit WaveformWidget(QWidget *parent = nullptr);

    void setProgress(double progress); // 0..1
    void setActive(bool active);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    double m_progress = 0;
    double m_phase = 0;
    QTimer m_timer;
};

// Small page symbols: sent, sending (partly filled), waiting.
class PageProgressWidget : public QWidget
{
public:
    explicit PageProgressWidget(QWidget *parent = nullptr);

    void setPages(int total, int done, bool sending);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_total = 0;
    int m_done = 0;
    bool m_sending = false;
};

// Numbered list of the steps of a send, each waiting, active, done or failed.
class StepList : public QWidget
{
public:
    enum class State { Waiting, Active, Done, Failed };

    explicit StepList(const QStringList &titles, QWidget *parent = nullptr);

    void setState(int index, State state);
    void setDetail(int index, const QString &detail);
    void reset();
    int count() const { return int(m_rows.size()); }

private:
    struct Row
    {
        QLabel *dot = nullptr;
        QLabel *title = nullptr;
        QLabel *detail = nullptr;
        State state = State::Waiting;
    };
    QList<Row> m_rows;
};

// Re-polishes a widget after a dynamic property used by the style sheet changed.
void setStyleProperty(QWidget *widget, const char *name, const QVariant &value);

} // namespace faxwerk::gui
