# ESP32 Sensor Tester

Cross-platform desktop control and data-recording app for an ESP32, two FSR sensors, and a step/direction motor driver. It communicates in both directions over USB serial and can use the ESP32 Wi-Fi access point as a fallback.

## What it does

- ESP32 firmware acquires both FSR channels at 10 kHz per sensor using the ESP-IDF continuous ADC DMA driver.
- Automatically captures exactly 0.5 seconds when either FSR crosses the configured threshold, then dumps one fixed-size raw binary block over USB.
- Sends motor direction, number of steps, pulse delay, and stop commands.
- Labels each recording with a test type/name.
- Exports recorded measurements to CSV or a formatted Excel workbook with a summary chart.
- Supports macOS and Windows from the same Python source.

## 1. Upload the ESP32 firmware

Open `firmware/esp32_sensor_tester/esp32_sensor_tester.ino` in Arduino IDE and upload it.

The timer and ADC calls use the ESP32 Arduino Core 3.x API. The current sketch was compile-checked against Arduino-ESP32 3.3.8 for the classic **ESP32 Dev Module**. If **Move up** physically moves down, swap `UP_DIRECTION_LEVEL` and `DOWN_DIRECTION_LEVEL` near the top of the sketch.

The firmware uses **460,800 baud** and accepts these USB serial commands, one per line. The installed USB-UART bridge was tested at 2,000,000 baud but dropped bytes, so the lower verified rate is used to protect measurement fidelity:

```
PING
MOVE,UP,200,500
MOVE,DOWN,200,500
STOP
RECORD,1
```

The DMA conversion pattern alternates ADC1 channel 4 (GPIO32) and ADC1 channel 5 (GPIO33) at 20,000 total conversions per second. This produces 10,000 complete FSR pairs per second. A 64-pair circular buffer retains 6.4 ms of pre-trigger history. When either FSR reaches `FSR_TRIGGER_THRESHOLD`, the firmware fills a 5,000-pair capture and queues it from a dedicated USB task. `RECORD,1` is retained as an optional manual software trigger.

Every capture is exactly **20,000 bytes**, with no header, timestamps, ASCII values, or delimiters. Values are unsigned 16-bit little-endian integers interleaved as:

```text
fsr1[0], fsr2[0], fsr1[1], fsr2[1], ... fsr1[4999], fsr2[4999]
```

No ESP32-to-computer acknowledgements are printed, because text bytes would corrupt the fixed-length binary stream. Motor commands are still accepted over USB, and the existing HTTP endpoints (`/sensors`, `/move`, and `/stop`) remain available over Wi-Fi. DMA acquisition and USB dumping run in dedicated FreeRTOS tasks; the motor hardware-timer ISR remains independent.

The Python desktop app opens USB at 460,800 baud and reads each fixed 20,000-byte block without waiting on long blocking reads. It decodes 5,000 sample pairs at 100 µs spacing, automatically stops the UI recording when the complete block arrives, and also accepts autonomous pressure-triggered captures. The live plot still draws only approximately every 200 ms while CSV/Excel retain all 5,000 samples.

## 2. Run from Python

Python 3.10 or newer is recommended. It must include Tkinter (the standard Python installer from [python.org](https://www.python.org/downloads/) does). If a Homebrew Python reports that `_tkinter` is missing, install its matching `python-tk` formula or use the python.org build.

### macOS / Linux

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
python run_app.py
```

### Windows

```bat
py -m venv .venv
.venv\Scripts\activate
python -m pip install -r requirements.txt
python run_app.py
```

For USB, connect the ESP32, choose **USB**, refresh the port list, and connect. For Wi-Fi, join `ESP32_Actuator_Control` with password `password123`, choose **Wi-Fi**, and connect to `http://192.168.4.1`.

On Windows, if no COM port appears, install the USB driver used by the board (commonly CP210x or CH340). On macOS, accept any USB accessory prompt and allow the app access if requested.

## 3. Build the standalone application

Run `build_macos.sh` on a Mac to create `dist/SensorTester.app`.

```bash
chmod +x build_macos.sh
./build_macos.sh
```

Run `build_windows.bat` on Windows to create the self-contained `dist\SensorTester.exe`. Send that single file to the other Windows computer.

PyInstaller does not cross-compile: build the `.app` on macOS and the `.exe` on Windows. The output is self-contained for users; Python does not need to be installed on the target computer. Some operating systems may warn about unsigned apps. Code signing is recommended before distributing publicly.

## Recorded columns

Every recorded and exported row contains only `time_ms`, `fsr1`, and `fsr2`. Time starts at zero for each recording. The live graph only retains its last 30 seconds, but recording retains every high-rate sample until **Clear** is used, so CSV and Excel exports contain the full recording. Raw FSR readings are ADC counts (0–4095); force units require calibration for the particular sensor and mechanical setup.

The raw binary block is converted to the three-column CSV/Excel format by the Python app. Since the firmware intentionally sends no timestamps, the app reconstructs `time_ms` from the fixed 100 µs sample spacing.

When recording stops, the app estimates response delay from the first point at which each sensor crosses 10% of its own recorded range. This is an onset estimate; noise, preload, saturation, and different sensor sensitivities can affect it.

## Safety

Test the direction with a small step count first. The red stop button sends a software stop, but machinery should also have physical limit switches and a hardware emergency stop. The firmware does not currently enforce travel limits.
