#pragma once

#include <QtCore/QtGlobal>

namespace sensor::constants {

constexpr qint32 UsbBaudRate = 115200;
constexpr int BytesPerCapturePair = 4;
constexpr double SampleIntervalMs = 2.0;
constexpr double GraphWindowSeconds = 10.0;
constexpr int MinDisplayIntervalMs = 1;
constexpr int DefaultDisplayIntervalMs = 200;
constexpr int MaxDisplayIntervalMs = 500;
constexpr int MinWifiPollMs = 100;
constexpr int WifiPollMs = 200;
constexpr int MinCapturePreviewStride = 1;  // ADS1256 acquisition task emits one AIN1/AIN2 pair every 2 ms.
constexpr int AdcMaxValue = 5000;

}  // namespace sensor::constants
