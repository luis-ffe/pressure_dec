#include "MotorControlDialog.h"

#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

namespace sensor {

MotorControlDialog::MotorControlDialog(int steps, int delayUs, QWidget* parent) : QDialog(parent) {
    setWindowTitle("Motor controls");
    setModal(false);
    resize(360, 230);

    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel("Set motor movement values here, then move the actuator directly from this window.");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* form = new QFormLayout();
    stepsSpin_ = new QSpinBox();
    stepsSpin_->setRange(1, 10000000);
    stepsSpin_->setValue(steps);
    delaySpin_ = new QSpinBox();
    delaySpin_->setRange(20, 5000);
    delaySpin_->setValue(delayUs);
    delaySpin_->setSuffix(" µs");
    form->addRow("Motor steps", stepsSpin_);
    form->addRow("Pulse delay", delaySpin_);
    layout->addLayout(form);

    auto* moveRow = new QHBoxLayout();
    auto* upButton = new QPushButton("▲ Move up");
    auto* downButton = new QPushButton("▼ Move down");
    moveRow->addWidget(upButton);
    moveRow->addWidget(downButton);
    layout->addLayout(moveRow);

    auto* stopButton = new QPushButton("STOP MOTOR");
    stopButton->setObjectName("dangerButton");
    layout->addWidget(stopButton);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close);
    layout->addWidget(buttons);

    connect(stepsSpin_, qOverload<int>(&QSpinBox::valueChanged), this, &MotorControlDialog::emitSettings);
    connect(delaySpin_, qOverload<int>(&QSpinBox::valueChanged), this, &MotorControlDialog::emitSettings);
    connect(upButton, &QPushButton::clicked, this, [this] { emitMove("up"); });
    connect(downButton, &QPushButton::clicked, this, [this] { emitMove("down"); });
    connect(stopButton, &QPushButton::clicked, this, &MotorControlDialog::stopRequested);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        emitSettings();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void MotorControlDialog::emitSettings() {
    emit settingsChanged(stepsSpin_->value(), delaySpin_->value());
}

void MotorControlDialog::emitMove(const QString& direction) {
    emitSettings();
    emit moveRequested(direction, stepsSpin_->value(), delaySpin_->value());
}

}  // namespace sensor
