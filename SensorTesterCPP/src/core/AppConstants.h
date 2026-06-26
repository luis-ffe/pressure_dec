#pragma once

#include <QtCore/QtGlobal>

namespace sensor::constants {

constexpr qint32 UsbBaudRate = 460800;
constexpr int CapturePairs = 5000;
constexpr int BytesPerCapturePair = 4;
constexpr int CaptureBytes = CapturePairs * BytesPerCapturePair;
constexpr double SampleIntervalMs = 0.1;
constexpr double GraphWindowSeconds = 30.0;
constexpr int PlotEveryPairs = 2000;  // 10 kHz samples, UI plot around every 200 ms.
constexpr int WifiPollMs = 200;
constexpr int AdcMaxValue = 4095;

}  // namespace sensor::constants
