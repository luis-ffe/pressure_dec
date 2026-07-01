# SensorTesterCPP

Native C++/Qt version of the ESP32 Sensor Tester UI.

All C++ app source, build scripts, and generated build files for this version
live inside `SensorTesterCPP/`.

## Features

- USB serial connection at 115,200 baud.
- ADS1256 external ADC support: FSR1 on AIN1 and FSR2 on AIN2, streamed as
  scaled 16-bit values using fixed PGA gain 1 and 64-bit-safe scaling.
- Wi-Fi control through the existing ESP32 HTTP endpoints.
- Motor move up/down, stop, and automated test commands.
- Motor values and manual motor movement live in a dedicated Motor dialog.
- Recording setup dialog for graph mode, max pressure display, loop/time options,
  saved acquisition interval, and UI graph display interval.
- Manual ADS1256 capture command over USB.
- Automatic capture display when the ESP32 sends raw binary sample pairs.
- Decodes marker-delimited interleaved little-endian sample pairs:

  ```text
  fsr1[0], fsr2[0], fsr1[1], fsr2[1], ...
  ```

- Saves all recorded high-frequency values while plotting only a lightweight
  live view.
- CSV export.
- Excel-openable XML export (`.xls`). Excel can open it directly; use “Save As”
  in Excel if you need a native `.xlsx` workbook.
- Separate `FsrLiveViewer.app` for a minimal FSR1/FSR2 graph. It connects over
  USB, starts the ESP32 binary stream, keeps only the latest 5 seconds, and
  fixes the graph range to 0–6000 ADC.

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
always throttled to the selected display interval between 1 ms and 500 ms.
USB recording can still save downsampled data from the ESP32's 2 ms ADS1256 stream.
During USB binary capture, the serial layer forwards a live graph preview every
2 ms and the UI applies the selected display throttle on top of that.
Wi-Fi preview/acquisition is still clamped to a 100 ms minimum because the
current ESP32 Wi-Fi protocol uses HTTP sensor reads.

For USB preview, flash the matching ESP32 firmware from
`firmware/esp32_sensor_tester/esp32_sensor_tester.ino`. It reads ADS1256 AIN1
and AIN2 with PGA gain 1, sends compact `S,time_ms,fsr1,fsr2` preview lines
every 200 ms while idle, then switches to marker-delimited binary streaming
while recording.

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

To run only the lightweight FSR graph viewer:

```bash
open build/bin/FsrLiveViewer.app
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
