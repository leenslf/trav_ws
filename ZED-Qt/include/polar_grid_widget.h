#pragma once

#include <QWidget>
#include <QElapsedTimer>
#include <QPointF>
#include <optional>
#include "frame_data.h"

class QWheelEvent;
class QMouseEvent;

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
        float polar_grid_size_theta_deg = 1.0f;
    };

    explicit PolarGridWidget(QWidget* parent = nullptr);
    void setConfig(const Config& cfg);

public slots:
    void updateFrame(const FrameData& frame);

protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;

private:
    Config   m_config;
    std::optional<FrameData> m_currentFrame;
    int      m_frameCount      = 0;
    double   m_fps             = 0.0;
    QElapsedTimer m_fpsTimer;
    int      m_framesInWindow  = 0;

    // Zoom / pan state. Screen coords = origin - world*(baseScale*zoom) + panOffset.
    float    m_zoom            = 1.0f;
    QPointF  m_panOffset       = {0.0f, 0.0f};
    bool     m_panning         = false;
    QPoint   m_lastMousePos;

    // Cached from the last paintEvent so wheel/mouse handlers can invert the
    // world<->screen mapping without redoing the fit-to-widget computation.
    float    m_baseScale       = 1.0f;
    float    m_originX         = 0.0f;
    float    m_originY         = 0.0f;
};
