#pragma once

#include <QByteArray>
#include <QImage>
#include <QWidget>

#include "frame_data.h"

class JpegViewerWidget : public QWidget {
    Q_OBJECT
public:
    explicit JpegViewerWidget(QWidget* parent = nullptr);

public slots:
    void updateImage(const QByteArray& bytes);
    void updateFrame(const FrameData& frame);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QImage    m_currentImage;
    FrameData m_frame{};
    bool      m_hasFrame{false};
};
