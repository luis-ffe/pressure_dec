#pragma once

#include <QtCore/QVector>
#include <QtWidgets/QWidget>

#include "../model/RecordingSettings.h"

namespace sensor {

class LivePlotWidget final : public QWidget {
    Q_OBJECT

public:
    explicit LivePlotWidget(QWidget* parent = nullptr);

    void clear();
    void addPoint(double timeSeconds, quint16 fsr1, quint16 fsr2);
    void setMaxValue(int maxValue);
    void setGraphMode(GraphMode mode);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct PlotPoint {
        double timeSeconds = 0.0;
        quint16 fsr1 = 0;
        quint16 fsr2 = 0;
    };

    QPoint mapPoint(const QRect& plotRect, double startSeconds, double seconds, quint16 value) const;

    QVector<PlotPoint> points_;
    int maxValue_ = 4095;
    GraphMode graphMode_ = GraphMode::DualLines;
};

}  // namespace sensor
