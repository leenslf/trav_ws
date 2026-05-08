#pragma once

#include <QWidget>
#include <QElapsedTimer>
#include <optional>
#include "frame_data.h"

class PolarGridWidget : public QWidget {
    Q_OBJECT
public:
    struct Config {
        float r_min_m                   = 0.3f;
        float r_max_m                   = 2.0f;
        float theta_min_deg             = -45.0f;
        float theta_max_deg             =  45.0f;
        float danger_threshold          = 0.3f;
        float scrit_deg                 = 30.0f;
        float rcrit_m                   = 0.10f;
        float hcrit_m                   = 0.20f;
        float polar_grid_size_r_m       = 0.10f;
        float polar_grid_size_theta_deg = 5.0f;
    };

    explicit PolarGridWidget(QWidget* parent = nullptr);
    void setConfig(const Config& cfg);

public slots:
    void updateFrame(const FrameData& frame);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    Config   m_config;
    std::optional<FrameData> m_currentFrame;
    int      m_frameCount      = 0;
    double   m_fps             = 0.0;
    QElapsedTimer m_fpsTimer;
    int      m_framesInWindow  = 0;
};
