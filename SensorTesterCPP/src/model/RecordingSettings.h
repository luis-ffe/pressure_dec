#pragma once

#include <algorithm>
#include <cmath>

#include <QtCore/QString>

#include "../core/AppConstants.h"
#include "../core/TestProfile.h"

namespace sensor {

enum class GraphMode {
    DualLines,
    FilledComparison,
    Difference,
};

struct RecordingSettings {
    GraphMode graphMode = GraphMode::DualLines;
    int maxPressure = constants::AdcMaxValue;
    bool loopEnabled = false;
    int timeLimitSeconds = 0;       // 0 means no limit.
    double acquisitionIntervalMs = 0.1;  // 0.1 ms = 100 microseconds.
    int displayIntervalMs = 200;    // UI graph is intentionally low-rate.
    TestProfileSettings testProfile;

    [[nodiscard]] int acquisitionStrideForUsbCapture() const {
        const auto stride = static_cast<int>(std::lround(acquisitionIntervalMs / constants::SampleIntervalMs));
        return std::max(1, stride);
    }

    [[nodiscard]] int displayStrideForUsbCapture() const {
        const auto stride = static_cast<int>(std::lround(static_cast<double>(displayIntervalMs) / constants::SampleIntervalMs));
        return std::max(1, stride);
    }

    [[nodiscard]] double acquisitionIntervalSeconds() const {
        return acquisitionIntervalMs / 1000.0;
    }
};

inline QString graphModeName(GraphMode mode) {
    switch (mode) {
        case GraphMode::DualLines:
            return "Dual line graph";
        case GraphMode::FilledComparison:
            return "Filled comparison";
        case GraphMode::Difference:
            return "Sensor difference";
    }
    return "Dual line graph";
}

}  // namespace sensor
