#include "ClosedLoopController.h"

#include <cmath>

namespace sensor {

bool PressureControlDecision::shouldMove() const {
    return action != PressureControlAction::Hold;
}

bool PressureControlDecision::increasesPressure() const {
    return action == PressureControlAction::IncreasePressure;
}

QString PressureControlDecision::legacySerialCommand() const {
    if (!shouldMove()) {
        return {};
    }
    return QString("%1,%2,%3\n")
        .arg(increasesPressure() ? "F" : "B")
        .arg(commandSteps)
        .arg(commandDelayUs);
}

ClosedLoopController::ClosedLoopController(TestProfileSettings settings)
    : profile_(settings) {}

void ClosedLoopController::setProfile(TestProfileSettings settings) {
    profile_.setSettings(settings);
}

double ClosedLoopController::targetPressureAt(double elapsedSeconds) const {
    return profile_.targetPressureAt(elapsedSeconds);
}

PressureControlDecision ClosedLoopController::decide(double elapsedSeconds, double actualPressure) const {
    const TestProfileSettings& settings = profile_.settings();
    const double targetPressure = profile_.targetPressureAt(elapsedSeconds);
    const double error = targetPressure - actualPressure;

    PressureControlDecision decision{
        targetPressure,
        actualPressure,
        error,
        PressureControlAction::Hold,
        settings.commandSteps,
        settings.commandDelayUs,
    };

    if (std::abs(error) <= settings.controlDeadband) {
        return decision;
    }

    decision.action = error > 0.0
        ? PressureControlAction::IncreasePressure
        : PressureControlAction::DecreasePressure;
    return decision;
}

std::optional<QString> ClosedLoopController::commandFor(double elapsedSeconds, double actualPressure) const {
    const PressureControlDecision decision = decide(elapsedSeconds, actualPressure);
    if (!decision.shouldMove()) {
        return std::nullopt;
    }
    return decision.legacySerialCommand();
}

}  // namespace sensor
