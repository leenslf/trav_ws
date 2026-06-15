#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>

#include "frame_data.h"
#include "comm_receiver.h"
#include "polar_grid_widget.h"
#include "jpeg_viewer_widget.h"
#include "global_map_module.h"
#include "global_map_widget.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    qRegisterMetaType<FrameData>("FrameData");

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("ZED Polar Viewer"));
    window.resize(900, 700);

    // ── Widgets ──────────────────────────────────────────────────────────────
    auto *polar_grid_widget  = new PolarGridWidget;
    auto *jpeg_viewer_widget = new JpegViewerWidget;
    auto *global_map_module  = new GlobalMapModule(&window);
    auto *global_map_widget  = new GlobalMapWidget(global_map_module);

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
