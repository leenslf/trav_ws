#include <QApplication>
#include <QMainWindow>

#include "frame_data.h"
#include "comm_map_receiver.h"
#include "polar_grid_widget.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    qRegisterMetaType<FrameData>("FrameData");

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("ZED Polar Viewer"));
    window.resize(600, 600);

    auto *widget = new PolarGridWidget(&window);
    window.setCentralWidget(widget);

    auto *receiver = new CommMapReceiver(5000, &window);
    QObject::connect(receiver, &CommMapReceiver::frameReceived,
                     widget,   &PolarGridWidget::updateFrame);

    window.show();
    return app.exec();
}
