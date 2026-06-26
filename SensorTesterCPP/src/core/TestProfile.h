#pragma once

#include <QtCore/QString>

namespace sensor {

enum class TestProfileType {
    StepHold,
    LinearRampTriangle,
    CyclicSinusoidal,
};

struct TestProfileSettings {
    TestProfileType type = TestProfileType::StepHold;
    double peakForce = 2000.0;      // ADC/raw pressure units until calibrated.
    double durationSeconds = 5.0;   // Step hold length or full triangle period.
    double frequencyHz = 1.0;       // Cyclic fatigue frequency.
    double controlIntervalMs = 20.0;
    double controlDeadband = 10.0;
    int commandSteps = 10;
    int commandDelayUs = 80;

    [[nodiscard]] double amplitude() const;
    [[nodiscard]] double offset() const;
};

class TestProfileGenerator {
public:
    explicit TestProfileGenerator(TestProfileSettings settings = {});

    void setSettings(TestProfileSettings settings);
    [[nodiscard]] const TestProfileSettings& settings() const;
    [[nodiscard]] double targetPressureAt(double elapsedSeconds) const;

private:
    TestProfileSettings settings_;
};

[[nodiscard]] QString profileTypeName(TestProfileType type);

}  // namespace sensor
