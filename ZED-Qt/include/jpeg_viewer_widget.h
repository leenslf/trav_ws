#pragma once

#include <QByteArray>
#include <QImage>
#include <QWidget>

class JpegViewerWidget : public QWidget {
    Q_OBJECT
public:
    explicit JpegViewerWidget(QWidget* parent = nullptr);

public slots:
    void updateImage(const QByteArray& bytes);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QImage m_currentImage;
};
