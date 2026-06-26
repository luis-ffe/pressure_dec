#include "SerialPortEnumerator.h"

#include <QtSerialPort/QSerialPortInfo>

namespace sensor {

QList<SerialPortDescriptor> SerialPortEnumerator::availablePorts() {
    QList<SerialPortDescriptor> ports;
    for (const QSerialPortInfo& port : QSerialPortInfo::availablePorts()) {
        ports.push_back({
            port.portName() + " — " + port.description(),
            port.systemLocation(),
        });
    }
    return ports;
}

}  // namespace sensor
