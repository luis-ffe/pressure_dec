#pragma once

#include <memory>

#include <QtCore/QString>

#include "Transport.h"

namespace sensor {

struct TransportConfig {
    Transport::Kind kind = Transport::Kind::Usb;
    QString serialPort;
    QString wifiBaseUrl;
    int wifiPollIntervalMs = 200;
};

class TransportFactory {
public:
    [[nodiscard]] static std::unique_ptr<Transport> create(const TransportConfig& config);
};

}  // namespace sensor
