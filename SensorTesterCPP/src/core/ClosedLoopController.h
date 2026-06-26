#pragma once

#include <optional>

#include <QtCore/QString>

#include "TestProfile.h"

namespace sensor {

enum class PressureControlAction {
    Hold,
    IncreasePressure,
    DecreasePressure,
};

struct PressureControlDecision {
    double targetPressure = 0.0;
    double actualPressure = 0.0;
    double error = 0.0;
    PressureControlAction action = PressureControlAction::Hold;
    int commandSteps = 0;
    int commandDelayUs = 0;

    [[nodiscard]] bool shouldMove() const;
    [[nodiscard]] bool increasesPressure() const;
    [[nodiscard]] QString legacySerialCommand() const;
};

class ClosedLoopController {
public:
    explicit ClosedLoopController(TestProfileSettings settings = {});

    void setProfile(TestProfileSettings settings);
    [[nodiscard]] double targetPressureAt(double elapsedSeconds) const;
    [[nodiscard]] PressureControlDecision decide(double elapsedSeconds, double actualPressure) const;
    [[nodiscard]] std::optional<QString> commandFor(double elapsedSeconds, double actualPressure) const;

private:
    TestProfileGenerator profile_;
};

}  // namespace sensor
