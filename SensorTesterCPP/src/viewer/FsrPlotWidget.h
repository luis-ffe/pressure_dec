#pragma once

#include <QtCore/QVector>
#include <QtWidgets/QWidget>

namespace sensor::viewer {

class FsrPlotWidget final : public QWidget {
    Q_OBJECT

public:
    explicit FsrPlotWidget(QWidget* parent = nullptr);

    void clear();
    void addSample(double timeSeconds, quint16 fsr1, quint16 fsr2);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct Sample {
        double timeSeconds = 0.0;
        quint16 fsr1 = 0;
        quint16 fsr2 = 0;
    };

    QPoint mapPoint(const QRect& plotRect, double startSeconds, double timeSeconds, quint16 value) const;

    QVector<Sample> samples_;
    double latestTimeSeconds_ = 0.0;
};

}  // namespace sensor::viewer
