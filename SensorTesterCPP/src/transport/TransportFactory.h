#pragma once

#include <memory>

#include <QtCore/QString>

#include "../core/AppConstants.h"
#include "Transport.h"

namespace sensor {

struct TransportConfig {
    Transport::Kind kind = Transport::Kind::Usb;
    QString serialPort;
    QString wifiBaseUrl;
    int wifiPollIntervalMs = constants::WifiPollMs;
};

class TransportFactory {
public:
    [[nodiscard]] static std::unique_ptr<Transport> create(const TransportConfig& config);
};

}  // namespace sensor
