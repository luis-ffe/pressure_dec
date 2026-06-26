# SensorTesterCPP

Native C++/Qt version of the ESP32 Sensor Tester UI.

All C++ app source, build scripts, and generated build files for this version
live inside `SensorTesterCPP/`.

## Features

- USB serial connection at 460,800 baud.
- Wi-Fi control through the existing ESP32 HTTP endpoints.
- Motor move up/down, stop, and automated test commands.
- Motor values and manual motor movement live in a dedicated Motor dialog.
- Recording setup dialog for graph mode, max pressure display, loop/time options,
  saved acquisition interval, and low-rate UI display interval.
- Manual 0.5 s DMA capture command over USB.
- Automatic capture display when the ESP32 sends a 20,000-byte raw binary block.
- Decodes 5,000 interleaved little-endian sample pairs:

  ```text
  fsr1[0], fsr2[0], fsr1[1], fsr2[1], ... fsr1[4999], fsr2[4999]
  ```

- Saves all recorded high-frequency values while plotting only a lightweight
  live view.
- CSV export.
- Excel-openable XML export (`.xls`). Excel can open it directly; use “Save As”
  in Excel if you need a native `.xlsx` workbook.

## Code layout

```text
src/
  core/
    AppConstants.h       Shared protocol/timing constants
    ClosedLoopController.* Target-vs-actual command generator
    DataExporter.*       CSV and Excel-openable XML export
    DelayAnalyzer.*      Sensor response-delay calculation
    TestProfile.*        Step, triangle, and sinusoidal target profiles
  model/
    Measurement.h        Data structs shared across the app
  transport/
    Transport.h          Abstract transport interface
    TransportFactory.*   Factory for USB/Wi-Fi transport creation
    SerialTransport.*    USB serial implementation
    WifiTransport.*      Wi-Fi HTTP implementation
    SerialPortEnumerator.* Port discovery
  ui/
    AppStyle.*           Centralized Qt styling
    MainWindow.*         Application controller and UI composition
    LivePlotWidget.*     Custom fixed-window graph widget
    ProfileCurveWidget.* 10-second target profile preview
  main.cpp               Small Qt application entry point
examples/
  closed_loop_execution_example.cpp
```

The main design split is the `Transport` interface. The UI talks to one
transport abstraction and does not need to know whether commands are going over
USB serial or Wi-Fi HTTP. Transport creation is isolated in `TransportFactory`.
Closed-loop profile math, delay calculation, export, plotting widgets, and app
styling are separate modules so they can be tested or replaced without
rewriting the main window.

Live preview and recorded acquisition are intentionally separate. The graph is
always throttled to the selected low-rate interval between 100 ms and 500 ms.
USB recording can still save downsampled data from the ESP32's 100 µs DMA block.
Wi-Fi preview/acquisition stays low-rate because the current ESP32 Wi-Fi
protocol uses HTTP sensor reads.

For USB preview, flash the matching ESP32 firmware from
`firmware/esp32_sensor_tester/esp32_sensor_tester.ino`. It sends compact
`S,time_ms,fsr1,fsr2` preview lines every 200 ms while idle, then switches to
marker-delimited binary streaming only while recording.

The Recording Setup dialog also contains a Custom Curve / Test Profile section.
It supports Step Hold, Linear Ramp / Triangle, and Cyclic / Sinusoidal profiles.
For cyclic tests, amplitude and offset are derived from the peak force so the
target stays between 0 and the configured maximum pressure.

Automated Test uses the selected profile as a closed-loop target. The app
compares the target pressure against the latest sensor pressure and sends small
pressure-increase/decrease nudges using the configured command steps and delay.

## Build on macOS

Qt 6 and CMake are required.

```bash
cd SensorTesterCPP
chmod +x scripts/build_macos.sh
./scripts/build_macos.sh
open build/bin/SensorTesterCPP.app
```

## Build on Windows

Install Qt 6 and CMake, then run from a Developer Command Prompt or Qt terminal:

```bat
cd SensorTesterCPP
scripts\build_windows.bat
```

The executable will be created inside `SensorTesterCPP\build`.

For a send-to-friend Windows build, run Qt's deployment tool after building:

```bat
windeployqt build\bin\Release\SensorTesterCPP.exe
```
