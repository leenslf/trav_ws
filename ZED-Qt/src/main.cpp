#include <QApplication>
#include <QMainWindow>
#include <QSplitter>

#include "frame_data.h"
#include "comm_receiver.h"
#include "polar_grid_widget.h"
#include "jpeg_viewer_widget.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    qRegisterMetaType<FrameData>("FrameData");
    qRegisterMetaType<PoseMsg>("PoseMsg");

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("ZED Polar Viewer"));
    window.resize(600, 600);

    auto *polar_grid_widget  = new PolarGridWidget(&window);
    auto *jpeg_viewer_widget = new JpegViewerWidget(&window);

    auto *splitter = new QSplitter(Qt::Horizontal, &window);
    splitter->addWidget(polar_grid_widget);
    splitter->addWidget(jpeg_viewer_widget);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    window.setCentralWidget(splitter);

    auto *receiver = new CommReceiver(&window);
    QObject::connect(receiver, &CommReceiver::frameReceived,
                     polar_grid_widget, &PolarGridWidget::updateFrame);
    QObject::connect(receiver, &CommReceiver::imageReceived,
                     jpeg_viewer_widget, &JpegViewerWidget::updateImage);
    QObject::connect(receiver, &CommReceiver::poseReceived,
                     jpeg_viewer_widget, &JpegViewerWidget::updatePose);

    window.show();
    return app.exec();
}
