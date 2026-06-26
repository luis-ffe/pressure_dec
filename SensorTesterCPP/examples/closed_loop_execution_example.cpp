// Example only: this file is not compiled into the app.
//
// It shows how to execute the reusable closed-loop controller every X ms.
// The generated commands are exactly:
//   F,10,80\n  when actual pressure is below target
//   B,10,80\n  when actual pressure is above target
//
// IMPORTANT: the current ESP32 firmware in this project accepts MOVE,...
// and AUTOTEST,... commands. Add F/B parsing to the firmware before sending
// these raw commands to the real actuator.

#include <QtCore/QElapsedTimer>
#include <QtCore/QTimer>
#include <QtSerialPort/QSerialPort>

#include "../src/core/ClosedLoopController.h"

using sensor::ClosedLoopController;
using sensor::TestProfileSettings;
using sensor::TestProfileType;

void startClosedLoopExample(QSerialPort& serial) {
    TestProfileSettings settings;
    settings.type = TestProfileType::CyclicSinusoidal;
    settings.peakForce = 2000.0;         // motor operates between 0 and 2000 ADC
    settings.durationSeconds = 10.0;
    settings.frequencyHz = 1.0;
    settings.controlIntervalMs = 20.0;   // X ms
    settings.controlDeadband = 10.0;
    settings.commandSteps = 10;
    settings.commandDelayUs = 80;

    auto* elapsed = new QElapsedTimer();
    elapsed->start();

    auto* timer = new QTimer(&serial);
    timer->setInterval(static_cast<int>(settings.controlIntervalMs));

    auto* controller = new ClosedLoopController(settings);

    QObject::connect(timer, &QTimer::timeout, &serial, [&serial, elapsed, controller]() {
        const double t = elapsed->elapsed() / 1000.0;

        // Replace this with the latest pressure decoded from your telemetry.
        const double actualPressure = 0.0;

        const auto command = controller->commandFor(t, actualPressure);
        if (command.has_value()) {
            serial.write(command->toUtf8());
            serial.flush();
        }
    });

    timer->start();
}
