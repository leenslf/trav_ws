#include "polar_grid_widget.h"

#include <QPainter>
#include <QPaintEvent>
#include <cmath>

static constexpr QRgb COLOR_UNKNOWN     = qRgb(128, 128, 128);
static constexpr QRgb COLOR_TRAVERSABLE = qRgb(0,   200, 0);
static constexpr QRgb COLOR_OBSTACLE    = qRgb(220, 0,   0);

PolarGridWidget::PolarGridWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(400, 400);
    m_fpsTimer.start();
}

void PolarGridWidget::updateFrame(const FrameData& frame)
{
    m_currentFrame = frame;

    ++m_framesInWindow;
    const double elapsed = m_fpsTimer.elapsed() / 1000.0;
    if (elapsed >= 1.0) {
        m_fps = m_framesInWindow / elapsed;
        m_framesInWindow = 0;
        m_fpsTimer.restart();
    }
    ++m_frameCount;

    update();
}

QImage PolarGridWidget::colorize(const FrameData& frame) const
{
    const int nr = frame.nr;
    const int nt = frame.nt;

    QImage img(nt, nr, QImage::Format_RGB888);

    for (int r = 0; r < nr; ++r) {
        uchar* line = img.scanLine(r);
        for (int t = 0; t < nt; ++t) {
            const float v = frame.trav_grid[r * nt + t];
            QRgb color;
            if (std::isnan(v)) {
                color = COLOR_UNKNOWN;
            } else if (v <= 0.5f) {
                color = COLOR_TRAVERSABLE;
            } else {
                color = COLOR_OBSTACLE;
            }
            line[t * 3 + 0] = qRed(color);
            line[t * 3 + 1] = qGreen(color);
            line[t * 3 + 2] = qBlue(color);
        }
    }

    return img.mirrored(true, true);
}

void PolarGridWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x2b, 0x2b, 0x2b));

    p.setPen(Qt::white);
    p.setFont(QFont(p.font().family(), 16));

    if (!m_currentFrame.has_value()) {
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Waiting for data..."));
        return;
    }

    const FrameData& f = *m_currentFrame;
    QImage img = colorize(f);

    // Fit image to widget preserving aspect ratio, centered
    const QSize widgetSize = size();
    QSize scaled = img.size().scaled(widgetSize, Qt::KeepAspectRatio);
    QRect destRect(
        (widgetSize.width()  - scaled.width())  / 2,
        (widgetSize.height() - scaled.height()) / 2,
        scaled.width(),
        scaled.height()
    );

    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(destRect, img);

    // Overlay: seq, fps, dimensions
    const QString overlay = QStringLiteral("seq=%1  fps=%2  %3x%4")
        .arg(f.seq)
        .arg(m_fps, 0, 'f', 1)
        .arg(f.nr)
        .arg(f.nt);
    p.setFont(QFont(p.font().family(), 10));
    p.setPen(Qt::black);
    p.drawText(rect().adjusted(9, 9, 0, 0), Qt::AlignTop | Qt::AlignLeft, overlay);
    p.setPen(Qt::white);
    p.drawText(rect().adjusted(8, 8, 0, 0), Qt::AlignTop | Qt::AlignLeft, overlay);
}
