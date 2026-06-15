#pragma once

#include <QTimer>
#include <QWidget>

class GlobalMapModule;

// Minimal validation widget: renders GlobalMap::cells() as a 2D raster.
// Colour convention matches PolarGridWidget: green=traversable, red=obstacle,
// grey=unknown(NaN).  No interactivity, legends, or axes — throwaway phase 4.
class GlobalMapWidget : public QWidget {
    Q_OBJECT
public:
    explicit GlobalMapWidget(GlobalMapModule* module, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    GlobalMapModule* module_;
    QTimer           timer_;
};
