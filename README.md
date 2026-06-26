# ESP32 Sensor Tester

Native C++/Qt desktop application and ESP32 firmware for controlling a linear actuator and recording two FSR pressure sensors.

This branch intentionally keeps only:

- `SensorTesterCPP/` — the C++/Qt desktop app.
- `firmware/` — the ESP32 Arduino firmware.

The previous Python/Tk/PyInstaller implementation has been removed from this branch.

## Desktop app

The C++ app lives in:

```text
SensorTesterCPP/
```

Build on macOS:

```bash
cd SensorTesterCPP
chmod +x scripts/build_macos.sh
./scripts/build_macos.sh
open build/bin/SensorTesterCPP.app
```

Build on Windows from a Qt/CMake-capable terminal:

```bat
cd SensorTesterCPP
scripts\build_windows.bat
```

See `SensorTesterCPP/README.md` for the app architecture and UI details.

## ESP32 firmware

The firmware lives in:

```text
firmware/esp32_sensor_tester/esp32_sensor_tester.ino
```

It targets the classic ESP32 using:

- FSR1: GPIO 32 / ADC1 channel 4
- FSR2: GPIO 33 / ADC1 channel 5
- Motor enable: GPIO 15
- Motor direction: GPIO 2
- Motor pulse: GPIO 16

USB serial runs at `460800` baud.

The firmware provides:

- Low-rate USB preview lines while idle:

  ```text
  S,time_ms,fsr1,fsr2
  ```

- High-rate binary capture while recording.
- Wi-Fi access point and HTTP endpoints for lower-rate operation.
- Motor commands for manual movement, emergency stop, and closed-loop curve nudges.

Main USB commands:

```text
PING
MOVE,UP,200,500
MOVE,DOWN,200,500
STOP
RECORD,1
RECORD,0
F,10,80
B,10,80
```

`F` increases pressure and `B` backs off pressure for automated curve following.

## Safety

Start with small step counts and low force targets. The software emergency stop is useful, but the machine should still have physical travel limits and a hardware emergency stop.
