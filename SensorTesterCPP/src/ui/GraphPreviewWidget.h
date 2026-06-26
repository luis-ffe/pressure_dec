#pragma once

#include <QtWidgets/QWidget>

#include "../model/RecordingSettings.h"

namespace sensor {

class GraphPreviewWidget final : public QWidget {
    Q_OBJECT

public:
    explicit GraphPreviewWidget(QWidget* parent = nullptr);

    void setMode(GraphMode mode);
    [[nodiscard]] GraphMode mode() const { return mode_; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    GraphMode mode_ = GraphMode::DualLines;
};

}  // namespace sensor
