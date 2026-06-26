#include "ClosedLoopController.h"

#include <cmath>

namespace sensor {

ClosedLoopController::ClosedLoopController(TestProfileSettings settings)
    : profile_(settings) {}

void ClosedLoopController::setProfile(TestProfileSettings settings) {
    profile_.setSettings(settings);
}

double ClosedLoopController::targetPressureAt(double elapsedSeconds) const {
    return profile_.targetPressureAt(elapsedSeconds);
}

std::optional<QString> ClosedLoopController::commandFor(double elapsedSeconds, double actualPressure) const {
    const TestProfileSettings& settings = profile_.settings();
    const double targetPressure = profile_.targetPressureAt(elapsedSeconds);
    const double error = targetPressure - actualPressure;

    if (std::abs(error) <= settings.controlDeadband) {
        return std::nullopt;
    }

    const QChar direction = error > 0.0 ? QChar('F') : QChar('B');
    return QString("%1,%2,%3\n").arg(direction).arg(settings.commandSteps).arg(settings.commandDelayUs);
}

}  // namespace sensor
