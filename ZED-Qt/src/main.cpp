#include <QApplication>
#include <QMainWindow>
#include <QSplitter>

#include "frame_data.h"
#include "comm_map_receiver.h"
#include "polar_grid_widget.h"
#include "comm_image_receiver.h"
#include "jpeg_viewer_widget.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    qRegisterMetaType<FrameData>("FrameData");

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

    auto *receiver = new CommMapReceiver(5000, &window);
    QObject::connect(receiver, &CommMapReceiver::frameReceived,
                     polar_grid_widget, &PolarGridWidget::updateFrame);

    auto *image_receiver = new CommImageReceiver(5000, &window);
    QObject::connect(image_receiver, &CommImageReceiver::imageReceived,
                     jpeg_viewer_widget, &JpegViewerWidget::updateImage);

    window.show();
    return app.exec();
}
