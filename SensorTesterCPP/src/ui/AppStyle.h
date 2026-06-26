#pragma once

class QApplication;
class QWidget;

namespace sensor {

class AppStyle {
public:
    static void apply(QApplication& app);
    static void applyTo(QWidget& widget);
};

}  // namespace sensor
