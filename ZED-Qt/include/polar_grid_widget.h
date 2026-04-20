#pragma once

#include <QWidget>
#include <QElapsedTimer>
#include <optional>
#include "frame_data.h"

class PolarGridWidget : public QWidget {
    Q_OBJECT
public:
    explicit PolarGridWidget(QWidget* parent = nullptr);

public slots:
    void updateFrame(const FrameData& frame);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QImage colorize(const FrameData& frame) const;

    std::optional<FrameData> m_currentFrame;
    int      m_frameCount      = 0;
    double   m_fps             = 0.0;
    QElapsedTimer m_fpsTimer;
    int      m_framesInWindow  = 0;
};
