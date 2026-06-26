#pragma once

#include <QtCore/QList>
#include <QtCore/QString>

namespace sensor {

struct SerialPortDescriptor {
    QString label;
    QString systemLocation;
};

class SerialPortEnumerator {
public:
    [[nodiscard]] static QList<SerialPortDescriptor> availablePorts();
};

}  // namespace sensor
