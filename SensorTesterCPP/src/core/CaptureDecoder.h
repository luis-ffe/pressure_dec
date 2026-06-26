#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QVector>

#include "../model/Measurement.h"

namespace sensor {

class CaptureDecoder {
public:
    void reset();
    void append(QByteArray bytes);

    [[nodiscard]] bool hasCompleteCapture() const;
    [[nodiscard]] int pendingByteCount() const;
    [[nodiscard]] QVector<Measurement> takeCapture();

private:
    QByteArray buffer_;
};

}  // namespace sensor
