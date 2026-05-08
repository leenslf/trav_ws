#include "polar_grid_widget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QPolygonF>
#include <cmath>

PolarGridWidget::PolarGridWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(400, 400);
    m_fpsTimer.start();
}

void PolarGridWidget::setConfig(const Config& cfg)
{
    m_config = cfg;
    update();
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

void PolarGridWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x2b, 0x2b, 0x2b));
    p.setRenderHint(QPainter::Antialiasing);

    p.setPen(Qt::white);
    p.setFont(QFont(p.font().family(), 16));

    if (!m_currentFrame.has_value()) {
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Waiting for data..."));
        return;
    }

    const FrameData& f = *m_currentFrame;
    const int nr = f.nr;
    const int nt = f.nt;

    const float r_min    = m_config.r_min_m;
    const float r_max    = m_config.r_max_m;
    const float dr       = m_config.polar_grid_size_r_m;
    const float dt_rad   = m_config.polar_grid_size_theta_deg * float(M_PI) / 180.0f;
    const float th0_rad  = m_config.theta_min_deg             * float(M_PI) / 180.0f;
    const float th1_rad  = m_config.theta_max_deg             * float(M_PI) / 180.0f;

    // Robot coord system: X forward, Y left.
    // Screen: robot at bottom-center; X forward → screen up, Y left → screen left.
    const float margin = 30.0f;
    const float availW = width()  - 2.0f * margin;
    const float availH = height() - 2.0f * margin;

    const float y_extent = r_max * std::max(std::abs(std::sin(th0_rad)),
                                            std::abs(std::sin(th1_rad)));
    const float scale = std::min(availW / (2.0f * y_extent), availH / r_max);

    const float ox = width()  / 2.0f;
    const float oy = margin + availH;

    auto toScreen = [&](float wx, float wy) -> QPointF {
        return { ox - wy * scale, oy - wx * scale };
    };

    // Qt arc angle convention: 0°=right, 90°=up, CCW positive.
    // world theta=0 (forward=up) → Qt 90°; mapping: qt_deg = 90 + world_deg
    const float qt_arc_start = 90.0f + m_config.theta_min_deg;
    const float qt_arc_span  = m_config.theta_max_deg - m_config.theta_min_deg;

    // ── Sectors ──────────────────────────────────────────────────────────────
    const QPen borderPen(QColor(0x2b, 0x2b, 0x2b), 0.8f);
    for (int ri = 0; ri < nr; ++ri) {
        const float r1 = r_min + ri * dr;
        const float r2 = r1 + dr;
        for (int ti = 0; ti < nt; ++ti) {
            const float v = f.trav_grid[ri * nt + ti];
            QColor fill;
            if (std::isnan(v))              fill = { 128, 128, 128 };
            else if (v <= m_config.danger_threshold) fill = { 0, 200, 0 };
            else                             fill = { 220, 0, 0 };

            const float a1 = th0_rad + ti * dt_rad;
            const float a2 = a1 + dt_rad;

            QPolygonF sector;
            sector << toScreen(r1 * std::cos(a1), r1 * std::sin(a1))
                   << toScreen(r2 * std::cos(a1), r2 * std::sin(a1))
                   << toScreen(r2 * std::cos(a2), r2 * std::sin(a2))
                   << toScreen(r1 * std::cos(a2), r1 * std::sin(a2));

            p.setPen(borderPen);
            p.setBrush(fill);
            p.drawPolygon(sector);
        }
    }

    // ── R ticks: concentric arcs at each cell boundary ────────────────────
    const QPen tickPen(QColor(220, 220, 220, 70), 0.6f);
    p.setBrush(Qt::NoBrush);
    for (int ri = 0; ri <= nr; ++ri) {
        const float rs = (r_min + ri * dr) * scale;
        QPainterPath arc;
        QRectF br(ox - rs, oy - rs, 2.0f * rs, 2.0f * rs);
        arc.arcMoveTo(br, qt_arc_start);
        arc.arcTo(br, qt_arc_start, qt_arc_span);
        p.setPen(tickPen);
        p.drawPath(arc);
    }

    // R labels: every 0.5 m along theta=0 (straight forward)
    const QFont tickFont(p.font().family(), 8);
    p.setFont(tickFont);
    p.setPen(QColor(210, 210, 210));
    for (float r = 0.5f; r <= r_max + 0.01f; r += 0.5f) {
        if (r < r_min) continue;
        const QPointF pos = toScreen(r, 0.0f);
        p.drawText(pos + QPointF(4, -2), QStringLiteral("%1m").arg(r, 0, 'f', 1));
    }

    // ── Theta ticks: radial spokes at each cell boundary ─────────────────
    p.setPen(tickPen);
    for (int ti = 0; ti <= nt; ++ti) {
        const float th = th0_rad + ti * dt_rad;
        p.drawLine(toScreen(r_min * std::cos(th), r_min * std::sin(th)),
                   toScreen(r_max * std::cos(th), r_max * std::sin(th)));
    }

    // Theta labels: every 15° just beyond r_max
    {
        p.setFont(tickFont);
        p.setPen(QColor(210, 210, 210));
        const float labelR = r_max + 16.0f / scale;   // 16 px beyond outer arc
        const QFontMetrics fm(tickFont);
        for (float deg = m_config.theta_min_deg; deg <= m_config.theta_max_deg + 0.01f; deg += 15.0f) {
            const float th = deg * float(M_PI) / 180.0f;
            const QPointF center = toScreen(labelR * std::cos(th), labelR * std::sin(th));
            const QString txt = QStringLiteral("%1°").arg(int(deg));
            const QSizeF ts = fm.boundingRect(txt).size();
            p.drawText(center - QPointF(ts.width() / 2.0, -ts.height() / 2.0), txt);
        }
    }

    // ── Robot dot ────────────────────────────────────────────────────────
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawEllipse(toScreen(0.0f, 0.0f), 4.0, 4.0);

    // ── Legend ───────────────────────────────────────────────────────────
    {
        const QFont legendFont(p.font().family(), 9);
        p.setFont(legendFont);
        const QFontMetrics lm(legendFont);
        const int sw = 12, sh = 12, gap = 5, rowH = 18;
        const int lx = width() - 110;
        int ly = 10;

        struct { QColor color; const char* label; } entries[] = {
            { {  0, 200,   0}, "Traversable" },
            { {220,   0,   0}, "Obstacle"    },
            { {128, 128, 128}, "Unknown"     },
        };
        for (auto& e : entries) {
            p.setPen(Qt::NoPen);
            p.setBrush(e.color);
            p.drawRect(lx, ly + (rowH - sh) / 2, sw, sh);
            p.setPen(Qt::white);
            p.drawText(lx + sw + gap, ly + rowH - (rowH - lm.ascent()) / 2 - 1, e.label);
            ly += rowH;
        }
    }

    // ── Overlay ──────────────────────────────────────────────────────────
    const QString overlay = QStringLiteral("seq=%1  fps=%2  %3×%4")
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
