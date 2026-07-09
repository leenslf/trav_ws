#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>

#include <filesystem>

#include "frame_data.h"
#include "comm_receiver.h"
#include "polar_grid_widget.h"
#include "jpeg_viewer_widget.h"
#include "global_map_module.h"
#include "global_map_widget.h"
#include "traversability/config.hpp"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    qRegisterMetaType<FrameData>("FrameData");

    // zed_qt lives at <repo>/ZED-Qt/build/zed_qt; config.yaml lives at <repo>/config/config.yaml.
    const std::string default_config =
        (std::filesystem::canonical("/proc/self/exe")
             .parent_path().parent_path().parent_path() / "config" / "config.yaml").string();
    const auto cfg = PipelineConfig::load_from_file(argc > 1 ? argv[1] : default_config);

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("ZED Polar Viewer"));
    window.resize(900, 700);

    // ── Widgets ──────────────────────────────────────────────────────────────
    auto *polar_grid_widget  = new PolarGridWidget;
    auto *jpeg_viewer_widget = new JpegViewerWidget;
    auto *global_map_module  = new GlobalMapModule(&window);
    auto *global_map_widget  = new GlobalMapWidget(global_map_module);

    // ── Config: drive both widgets from config.yaml's [traversability] block ──
    {
        const auto& t = cfg.traversability;

        PolarGridWidget::Config grid_cfg;
        grid_cfg.r_min_m                   = t.r_min_m;
        grid_cfg.r_max_m                   = t.r_max_m;
        grid_cfg.theta_min_deg             = t.theta_min_deg;
        grid_cfg.theta_max_deg             = t.theta_max_deg;
        grid_cfg.danger_threshold          = t.danger_threshold;
        grid_cfg.scrit_deg                 = t.scrit_deg;
        grid_cfg.rcrit_m                   = t.rcrit_m;
        grid_cfg.hcrit_m                   = t.hcrit_m;
        grid_cfg.polar_grid_size_r_m       = t.polar_grid_size_r_m;
        grid_cfg.polar_grid_size_theta_deg = t.polar_grid_size_theta_deg;
        polar_grid_widget->setConfig(grid_cfg);

        GlobalMapModule::GridConfig map_grid_cfg;
        map_grid_cfg.r_min_m       = t.r_min_m;
        map_grid_cfg.dr_m          = t.polar_grid_size_r_m;
        map_grid_cfg.theta_min_deg = t.theta_min_deg;
        map_grid_cfg.dtheta_deg    = t.polar_grid_size_theta_deg;
        global_map_module->setGridConfig(map_grid_cfg);
    }

    // ── Layout ───────────────────────────────────────────────────────────────
    // Outer: horizontal splitter  [left (3) | JpegViewer (1)]
    // Left:  vertical splitter    [local grid top | global map bottom]
    // Each half wrapped in a QWidget+QVBoxLayout so the label sits above the widget.

    auto make_labeled = [](const QString& title, QWidget* content) -> QWidget* {
        auto *container = new QWidget;
        auto *layout    = new QVBoxLayout(container);
        layout->setContentsMargins(2, 2, 2, 2);
        layout->setSpacing(0);
        auto *lbl = new QLabel(title);
        lbl->setStyleSheet(QStringLiteral(
            "color: white; background-color: #555555;"
            "font-size: 10px; padding: 2px 4px;"));
        lbl->setFixedHeight(18);
        layout->addWidget(lbl);
        layout->addWidget(content);
        return container;
    };

    auto *left_vsplitter = new QSplitter(Qt::Vertical);
    left_vsplitter->addWidget(make_labeled(QStringLiteral("Local Traversability Grid"),
                                           polar_grid_widget));
    left_vsplitter->addWidget(make_labeled(QStringLiteral("Global Map"),
                                           global_map_widget));
    left_vsplitter->setSizes({350, 350});

    auto *outer_splitter = new QSplitter(Qt::Horizontal, &window);
    outer_splitter->addWidget(left_vsplitter);
    outer_splitter->addWidget(jpeg_viewer_widget);
    outer_splitter->setStretchFactor(0, 3);
    outer_splitter->setStretchFactor(1, 1);
    window.setCentralWidget(outer_splitter);

    // ── Signal wiring ────────────────────────────────────────────────────────
    auto *receiver = new CommReceiver(&window);
    QObject::connect(receiver, &CommReceiver::frameReceived,
                     polar_grid_widget, &PolarGridWidget::updateFrame);
    QObject::connect(receiver, &CommReceiver::frameReceived,
                     jpeg_viewer_widget, &JpegViewerWidget::updateFrame);
    QObject::connect(receiver, &CommReceiver::frameReceived,
                     global_map_module, &GlobalMapModule::onFrameReceived);
    QObject::connect(receiver, &CommReceiver::imageReceived,
                     jpeg_viewer_widget, &JpegViewerWidget::updateImage);

    window.show();
    return app.exec();
}
