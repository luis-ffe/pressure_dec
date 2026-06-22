# ESP32 Sensor Tester

Cross-platform desktop control and data-recording app for an ESP32, two FSR sensors, and a step/direction motor driver. It communicates in both directions over USB serial and can use the ESP32 Wi-Fi access point as a fallback.

## What it does

- Displays the live graph at about 5 Hz (one point every 200 ms). When recording starts, the app commands the ESP32 to capture at 250 Hz while continuing to plot only every 200 ms.
- Sends motor direction, number of steps, pulse delay, and stop commands.
- Labels each recording with a test type/name.
- Exports recorded measurements to CSV or a formatted Excel workbook with a summary chart.
- Supports macOS and Windows from the same Python source.

## 1. Upload the ESP32 firmware

Open `firmware/esp32_sensor_tester/esp32_sensor_tester.ino` in Arduino IDE and upload it.

The timer calls use the ESP32 Arduino Core 3.x API, matching the original firmware. Select the correct ESP32 board and a 115200 baud upload speed. If **Move up** physically moves down, swap `UP_DIRECTION_LEVEL` and `DOWN_DIRECTION_LEVEL` near the top of the sketch.

The firmware uses 115200 baud and accepts these USB serial commands, one per line:

```
PING
MOVE,UP,200,500
MOVE,DOWN,200,500
STOP
```

It streams only `S,time_ms,fsr1,fsr2`. The idle rate is approximately 5 Hz; `RECORD,1` switches acquisition to 250 Hz (approximately 4 ms between sample pairs), and `RECORD,0` returns to idle. Acquisition runs in a dedicated FreeRTOS task; the existing motor hardware-timer interrupt remains independent. The HTTP endpoints (`/sensors`, `/move`, and `/stop`) remain available over Wi-Fi.

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

Sensor data and control commands use separate message formats. The high-rate USB sensor stream remains the compact `S,time_ms,fsr1,fsr2` format, and the Wi-Fi `/sensors` response contains the same three values as JSON. Motor and recording control remain explicit commands such as `MOVE,UP,200,500`, `STOP`, and `RECORD,1`, so shortening measurement rows does not remove any motor-control information.

When recording stops, the app estimates response delay from the first point at which each sensor crosses 10% of its own recorded range. This is an onset estimate; noise, preload, saturation, and different sensor sensitivities can affect it.

## Safety

Test the direction with a small step count first. The red stop button sends a software stop, but machinery should also have physical limit switches and a hardware emergency stop. The firmware does not currently enforce travel limits.
