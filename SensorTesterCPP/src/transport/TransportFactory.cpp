#include "TransportFactory.h"

#include "SerialTransport.h"
#include "WifiTransport.h"

namespace sensor {

std::unique_ptr<Transport> TransportFactory::create(const TransportConfig& config) {
    switch (config.kind) {
        case Transport::Kind::Usb:
            return std::make_unique<SerialTransport>(config.serialPort);
        case Transport::Kind::Wifi:
            return std::make_unique<WifiTransport>(config.wifiBaseUrl, config.wifiPollIntervalMs);
    }
    return nullptr;
}

}  // namespace sensor
