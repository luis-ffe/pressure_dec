#include "TestProfile.h"

#include <algorithm>
#include <cmath>

namespace sensor {

namespace {
constexpr double Pi = 3.14159265358979323846;
}

double TestProfileSettings::amplitude() const {
    return std::max(0.0, peakForce) / 2.0;
}

double TestProfileSettings::offset() const {
    return std::max(0.0, peakForce) / 2.0;
}

TestProfileGenerator::TestProfileGenerator(TestProfileSettings settings)
    : settings_(settings) {}

void TestProfileGenerator::setSettings(TestProfileSettings settings) {
    settings_ = settings;
}

const TestProfileSettings& TestProfileGenerator::settings() const {
    return settings_;
}

double TestProfileGenerator::targetPressureAt(double elapsedSeconds) const {
    const double t = std::max(0.0, elapsedSeconds);
    const double peak = std::max(0.0, settings_.peakForce);
    const double duration = std::max(0.001, settings_.durationSeconds);

    switch (settings_.type) {
        case TestProfileType::StepHold:
            return t <= duration ? peak : 0.0;

        case TestProfileType::LinearRampTriangle: {
            if (t >= duration) {
                return 0.0;
            }
            const double halfDuration = duration / 2.0;
            if (t <= halfDuration) {
                return peak * (t / halfDuration);
            }
            return peak * (1.0 - ((t - halfDuration) / halfDuration));
        }

        case TestProfileType::CyclicSinusoidal: {
            const double frequency = std::max(0.0, settings_.frequencyHz);
            const double raw = settings_.amplitude() * std::sin(2.0 * Pi * frequency * t) + settings_.offset();
            return std::clamp(raw, 0.0, peak);
        }
    }

    return 0.0;
}

QString profileTypeName(TestProfileType type) {
    switch (type) {
        case TestProfileType::StepHold:
            return "Step Hold";
        case TestProfileType::LinearRampTriangle:
            return "Linear Ramp / Triangle";
        case TestProfileType::CyclicSinusoidal:
            return "Cyclic / Sinusoidal";
    }
    return "Step Hold";
}

}  // namespace sensor
