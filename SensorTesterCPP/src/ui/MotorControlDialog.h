#pragma once

#include <QtWidgets/QDialog>
#include <QtWidgets/QSpinBox>

namespace sensor {

class MotorControlDialog final : public QDialog {
    Q_OBJECT

public:
    MotorControlDialog(int steps, int delayUs, QWidget* parent = nullptr);

signals:
    void settingsChanged(int steps, int delayUs);
    void moveRequested(QString direction, int steps, int delayUs);
    void stopRequested();

private:
    void emitSettings();
    void emitMove(const QString& direction);

    QSpinBox* stepsSpin_ = nullptr;
    QSpinBox* delaySpin_ = nullptr;
};

}  // namespace sensor
