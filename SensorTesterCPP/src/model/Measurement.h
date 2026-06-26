#pragma once

#include <QtCore/QString>
#include <QtCore/QtGlobal>

namespace sensor {

struct Measurement {
    double timeMs = 0.0;
    quint16 fsr1 = 0;
    quint16 fsr2 = 0;
};

struct SensorSample {
    double timeSeconds = 0.0;
    quint16 fsr1 = 0;
    quint16 fsr2 = 0;
    bool motionStatusKnown = false;
    bool moving = false;
    int stepsRemaining = 0;
};

struct DelayResult {
    double signedDelayMs = 0.0;
    QString summary;
};

}  // namespace sensor
