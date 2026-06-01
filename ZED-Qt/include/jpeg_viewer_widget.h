#pragma once

#include <QByteArray>
#include <QImage>
#include <QWidget>

#include "comm_receiver.h"

class JpegViewerWidget : public QWidget {
    Q_OBJECT
public:
    explicit JpegViewerWidget(QWidget* parent = nullptr);

public slots:
    void updateImage(const QByteArray& bytes);
    void updatePose(const PoseMsg& pose);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QImage  m_currentImage;
    PoseMsg m_pose{};
    bool    m_hasPose{false};
};
