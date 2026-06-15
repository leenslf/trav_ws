#include "jpeg_viewer_widget.h"

#include <QPainter>
#include <QPaintEvent>

JpegViewerWidget::JpegViewerWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(320, 180);
}

void JpegViewerWidget::updateImage(const QByteArray& bytes)
{
    m_currentImage = QImage::fromData(bytes);
    update();
}

void JpegViewerWidget::updateFrame(const FrameData& frame)
{
    m_frame = frame;
    m_hasFrame = true;
    update();
}

void JpegViewerWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x2b, 0x2b, 0x2b));

    if (m_currentImage.isNull()) {
        p.setPen(Qt::white);
        p.setFont(QFont(p.font().family(), 16));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("No image"));
    } else {
        const QImage scaled = m_currentImage.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QPoint offset((width() - scaled.width()) / 2, (height() - scaled.height()) / 2);
        p.drawImage(offset, scaled);
    }

    if (!m_hasFrame)
        return;

    static const struct { const char* name; QColor color; } kStates[] = {
        { "OK",                    Qt::green  },
        { "SEARCHING",             Qt::yellow },
        { "FPS_TOO_LOW",           Qt::yellow },
        { "SEARCHING_FLOOR_PLANE", Qt::yellow },
        { "UNAVAILABLE",           Qt::red    },
        { "LOOP_CLOSED",           Qt::green  },
    };
    const char* stateName = "UNKNOWN";
    QColor stateColor = Qt::red;
    if (m_frame.tracking_state < sizeof(kStates) / sizeof(kStates[0])) {
        stateName  = kStates[m_frame.tracking_state].name;
        stateColor = kStates[m_frame.tracking_state].color;
    }

    p.setFont(QFont(p.font().family(), 12));
    const QFontMetrics fm(p.font());
    const int lineH = fm.height() + 2;

    const QString stateLine = QStringLiteral("State: %1").arg(QLatin1String(stateName));
    const QString transLine = QStringLiteral("tx: %1  ty: %2  tz: %3")
        .arg(double(m_frame.tx), 0, 'f', 2)
        .arg(double(m_frame.ty), 0, 'f', 2)
        .arg(double(m_frame.tz), 0, 'f', 2);
    const QString quatLine = QStringLiteral("qx: %1  qy: %2  qz: %3  qw: %4")
        .arg(double(m_frame.qx), 0, 'f', 3)
        .arg(double(m_frame.qy), 0, 'f', 3)
        .arg(double(m_frame.qz), 0, 'f', 3)
        .arg(double(m_frame.qw), 0, 'f', 3);

    auto drawHudLine = [&](int row, const QString& text, const QColor& color) {
        const int y = 8 + row * lineH;
        p.setPen(Qt::black);
        p.drawText(rect().adjusted(0, y + 1, -7, 0), Qt::AlignTop | Qt::AlignRight, text);
        p.setPen(color);
        p.drawText(rect().adjusted(0, y, -8, 0), Qt::AlignTop | Qt::AlignRight, text);
    };

    drawHudLine(0, stateLine, stateColor);
    drawHudLine(1, transLine, Qt::white);
    drawHudLine(2, quatLine,  Qt::white);
}
