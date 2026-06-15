#include "global_map_widget.h"
#include "global_map_module.h"

#include <QPainter>
#include <QPaintEvent>
#include <cmath>

// Must match PolarGridWidget::Config::danger_threshold default.
static constexpr float kDangerThreshold = 0.3f;

GlobalMapWidget::GlobalMapWidget(GlobalMapModule* module, QWidget* parent)
    : QWidget(parent)
    , module_(module)
{
    setMinimumSize(200, 200);
    connect(&timer_, &QTimer::timeout, this, [this]{ update(); });
    timer_.start(100);  // 10 Hz repaint — sufficient for validation
}

void GlobalMapWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x2b, 0x2b, 0x2b));

    if (!module_->isInitialized()) {
        p.setPen(Qt::white);
        p.setFont(QFont(p.font().family(), 14));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Waiting for data..."));
        return;
    }

    const auto& cells = module_->globalMap().cells();
    const int   W     = module_->globalMap().width();
    const int   H     = module_->globalMap().height();

    // Render the map into a WxH image, then scale-blit to the widget rect.
    // Avoids 40 k individual fillRect calls per repaint.
    //
    // Orientation matches PolarGridWidget: forward (x_world / ix) = up,
    // body-left (y_world / iy) = left.
    //   col = H - 1 - iy  →  larger iy (more left) maps to smaller col (screen-left)
    //   row = W - 1 - ix  →  larger ix (more forward) maps to smaller row (screen-top)
    // Image is H wide × W tall so col ∈ [0,H-1] and row ∈ [0,W-1] always.
    QImage img(H, W, QImage::Format_RGB32);
    for (int iy = 0; iy < H; ++iy) {
        for (int ix = 0; ix < W; ++ix) {
            const float v = cells[static_cast<std::size_t>(iy * W + ix)];
            QRgb rgb;
            if (std::isnan(v))              rgb = qRgb(128, 128, 128);  // unknown
            else if (v <= kDangerThreshold) rgb = qRgb(  0, 200,   0);  // traversable
            else                            rgb = qRgb(220,   0,   0);  // obstacle
            img.setPixel(H - 1 - iy, W - 1 - ix, rgb);
        }
    }
    p.drawImage(rect(), img);

    // Cell-boundary gridlines — one line per cell edge.
    {
        const float sx_gl = static_cast<float>(width())  / H;
        const float sy_gl = static_cast<float>(height()) / W;
        p.setPen(QColor(0, 0, 0, 80));
        for (int row = 1; row < W; ++row)
            p.drawLine(QPointF(0.f, row * sy_gl), QPointF(width(), row * sy_gl));
        for (int col = 1; col < H; ++col)
            p.drawLine(QPointF(col * sx_gl, 0.f), QPointF(col * sx_gl, height()));
    }

    // Draw camera position marker (oriented triangle: tip = camera forward).
    float tx, ty, yaw;
    if (module_->lastCameraPose(tx, ty, yaw)) {
        const float res = module_->globalMap().resolution_m();
        const float ox  = module_->globalMap().origin_x();
        const float oy  = module_->globalMap().origin_y();

        const int ix = static_cast<int>(std::floor((tx - ox) / res));
        const int iy = static_cast<int>(std::floor((ty - oy) / res));

        if (ix >= 0 && ix < W && iy >= 0 && iy < H) {
            const int row = W - 1 - ix;
            const int col = H - 1 - iy;

            const float sx = static_cast<float>(width())  / H;
            const float sy = static_cast<float>(height()) / W;
            const float cx = (col + 0.5f) * sx;
            const float cy = (row + 0.5f) * sy;

            // Screen-space axes derived from yaw:
            //   +x_world = forward = up on screen  → fwd_screen  = (-sin(yaw), -cos(yaw))
            //   +y_world = left    = left on screen → perp_screen = (-cos(yaw),  sin(yaw))
            const float fwd_sx  = -std::sin(yaw);
            const float fwd_sy  = -std::cos(yaw);
            const float perp_sx = -std::cos(yaw);
            const float perp_sy =  std::sin(yaw);

            constexpr float kTip  =  9.f;   // px from center to tip
            constexpr float kBack =  5.f;   // px from center to base midpoint
            constexpr float kHalf =  6.f;   // px half-width of base

            const QPointF tip  (cx + kTip  * fwd_sx,
                                cy + kTip  * fwd_sy);
            const QPointF baseL(cx - kBack * fwd_sx + kHalf * perp_sx,
                                cy - kBack * fwd_sy + kHalf * perp_sy);
            const QPointF baseR(cx - kBack * fwd_sx - kHalf * perp_sx,
                                cy - kBack * fwd_sy - kHalf * perp_sy);

            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 0));   // yellow
            p.drawPolygon(QPolygonF({tip, baseL, baseR}));

            // Legend: triangle swatch + label, bottom-left corner.
            constexpr float kLx = 8.f, kLy_offset = 28.f;
            const float ly = height() - kLy_offset;
            const QPointF lt (kLx + 5.f, ly);
            const QPointF ll (kLx,        ly + 9.f);
            const QPointF lr (kLx + 10.f, ly + 9.f);
            p.setBrush(QColor(255, 255, 0));
            p.drawPolygon(QPolygonF({lt, ll, lr}));
            p.setPen(Qt::white);
            p.setFont(QFont(p.font().family(), 9));
            p.drawText(static_cast<int>(kLx) + 14, static_cast<int>(ly) + 9,
                       QStringLiteral("Robot"));
        }
    }

    // Legend: map resolution — always visible once the map is initialised.
    p.setPen(Qt::white);
    p.setFont(QFont(p.font().family(), 9));
    p.drawText(8, height() - 8,
               QString("Res: %1 m").arg(module_->globalMap().resolution_m(), 0, 'f', 2));
}
