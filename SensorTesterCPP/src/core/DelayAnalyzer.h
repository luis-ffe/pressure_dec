#pragma once

#include <optional>

#include <QtCore/QVector>

#include "../model/Measurement.h"

namespace sensor {

class DelayAnalyzer {
public:
    [[nodiscard]] static std::optional<DelayResult> calculate(const QVector<Measurement>& rows);
};

}  // namespace sensor
