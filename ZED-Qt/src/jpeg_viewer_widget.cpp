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

void JpegViewerWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x2b, 0x2b, 0x2b));

    if (m_currentImage.isNull()) {
        p.setPen(Qt::white);
        p.setFont(QFont(p.font().family(), 16));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("No image"));
        return;
    }

    const QImage scaled = m_currentImage.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint offset((width() - scaled.width()) / 2, (height() - scaled.height()) / 2);
    p.drawImage(offset, scaled);
}
