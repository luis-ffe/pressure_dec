#pragma once

#include <optional>

#include <QtCore/QString>

#include "TestProfile.h"

namespace sensor {

class ClosedLoopController {
public:
    explicit ClosedLoopController(TestProfileSettings settings = {});

    void setProfile(TestProfileSettings settings);
    [[nodiscard]] double targetPressureAt(double elapsedSeconds) const;
    [[nodiscard]] std::optional<QString> commandFor(double elapsedSeconds, double actualPressure) const;

private:
    TestProfileGenerator profile_;
};

}  // namespace sensor
