#include "DelayAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace sensor {

std::optional<DelayResult> DelayAnalyzer::calculate(const QVector<Measurement>& rows) {
    if (rows.size() < 2) {
        return std::nullopt;
    }

    auto [min1It, max1It] = std::minmax_element(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return a.fsr1 < b.fsr1;
    });
    auto [min2It, max2It] = std::minmax_element(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return a.fsr2 < b.fsr2;
    });

    const double range1 = max1It->fsr1 - min1It->fsr1;
    const double range2 = max2It->fsr2 - min2It->fsr2;
    if (range1 <= 0.0 || range2 <= 0.0) {
        return std::nullopt;
    }

    const double threshold1 = min1It->fsr1 + range1 * 0.10;
    const double threshold2 = min2It->fsr2 + range2 * 0.10;
    std::optional<double> onset1;
    std::optional<double> onset2;

    for (const Measurement& row : rows) {
        if (!onset1 && row.fsr1 >= threshold1) {
            onset1 = row.timeMs;
        }
        if (!onset2 && row.fsr2 >= threshold2) {
            onset2 = row.timeMs;
        }
        if (onset1 && onset2) {
            break;
        }
    }

    if (!onset1 || !onset2) {
        return std::nullopt;
    }

    const double signedDelay = *onset2 - *onset1;
    const double absDelay = std::abs(signedDelay);
    const QString leader = signedDelay > 0.0
                               ? "FSR 1 leads"
                               : signedDelay < 0.0 ? "FSR 2 leads" : "Sensors change together";

    return DelayResult{
        signedDelay,
        QString("Response delay: %1 by %2 ms").arg(leader, QString::number(absDelay, 'f', absDelay < 10.0 ? 1 : 0)),
    };
}

}  // namespace sensor
