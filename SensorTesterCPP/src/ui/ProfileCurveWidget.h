#pragma once

#include <QtWidgets/QWidget>

#include "../core/TestProfile.h"

namespace sensor {

class ProfileCurveWidget final : public QWidget {
    Q_OBJECT

public:
    explicit ProfileCurveWidget(QWidget* parent = nullptr);

    void setProfile(TestProfileSettings settings);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    TestProfileSettings settings_;
};

}  // namespace sensor
