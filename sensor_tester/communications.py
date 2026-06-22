from __future__ import annotations

import json
import queue
import threading
import time
from abc import ABC, abstractmethod
from dataclasses import dataclass
from typing import Any, Callable


class CommunicationError(RuntimeError):
    """Raised when the ESP32 connection cannot be used."""


@dataclass(frozen=True)
class Sample:
    device_time_ms: int
    fsr1_raw: int
    fsr2_raw: int
    moving: bool = False
    steps_remaining: int = 0
    device_time_us: int = 0
    fsr1_time_us: int = 0
    fsr2_time_us: int = 0
    sequence: int = 0
    dropped_samples: int = 0
    motion_status_known: bool = True

    @classmethod
    def from_payload(cls, payload: dict[str, Any]) -> "Sample":
        device_time_ms = int(payload.get("time_ms", payload.get("time", 0)))
        device_time_us = int(payload.get("time_us", device_time_ms * 1000))
        return cls(
            device_time_ms=device_time_ms,
            fsr1_raw=int(payload.get("fsr1", payload.get("f1", 0))),
            fsr2_raw=int(payload.get("fsr2", payload.get("f2", 0))),
            moving=bool(payload.get("moving", False)),
            steps_remaining=int(payload.get("steps_remaining", 0)),
            device_time_us=device_time_us,
            fsr1_time_us=int(payload.get("fsr1_time_us", device_time_us)),
            fsr2_time_us=int(payload.get("fsr2_time_us", device_time_us)),
            sequence=int(payload.get("sequence", 0)),
            dropped_samples=int(payload.get("dropped_samples", 0)),
            motion_status_known="moving" in payload or "steps_remaining" in payload,
        )

    @classmethod
    def from_compact_usb(cls, line: str) -> "Sample":
        """Parse the minimal S,time_ms,fsr1,fsr2 USB stream."""
        fields = line.split(",")
        if len(fields) == 4 and fields[0] == "S":
            time_ms, fsr1, fsr2 = (int(value) for value in fields[1:])
            time_us = time_ms * 1000
            return cls(
                device_time_ms=time_ms,
                device_time_us=time_us,
                fsr1_time_us=time_us,
                fsr2_time_us=time_us,
                fsr1_raw=fsr1,
                fsr2_raw=fsr2,
                motion_status_known=False,
            )
        # Retain parser compatibility with the earlier experimental stream.
        if len(fields) != 10 or fields[0] != "S":
            raise ValueError("Invalid compact USB sample")
        sequence, pair_us, fsr1_us, fsr1, fsr2_us, fsr2, moving, steps, dropped = (
            int(value) for value in fields[1:]
        )
        return cls(
            device_time_ms=pair_us // 1000,
            device_time_us=pair_us,
            fsr1_time_us=fsr1_us,
            fsr2_time_us=fsr2_us,
            fsr1_raw=fsr1,
            fsr2_raw=fsr2,
            moving=bool(moving),
            steps_remaining=steps,
            sequence=sequence,
            dropped_samples=dropped,
            motion_status_known=True,
        )


class Transport(ABC):
    name: str

    @abstractmethod
    def connect(self) -> None: ...

    @abstractmethod
    def close(self) -> None: ...

    @abstractmethod
    def read_sample(self) -> Sample | None: ...

    @abstractmethod
    def move(self, direction: str, steps: int, delay_us: int) -> None: ...

    @abstractmethod
    def stop(self) -> None: ...

    @abstractmethod
    def set_recording(self, active: bool) -> None: ...


class SerialTransport(Transport):
    name = "USB"

    def __init__(self, port: str, baudrate: int = 115200) -> None:
        self.port = port
        self.baudrate = baudrate
        self._serial: Any = None

    def connect(self) -> None:
        try:
            import serial

            self._serial = serial.serial_for_url(
                self.port, self.baudrate, timeout=0.25, write_timeout=1
            )
            time.sleep(1.5)  # Many ESP32 boards reset when the port opens.
            self._serial.reset_input_buffer()
            self._write("PING")
        except Exception as exc:
            self.close()
            raise CommunicationError(f"Could not open serial port {self.port}: {exc}") from exc

    def close(self) -> None:
        if self._serial is not None:
            try:
                self._write("RECORD,0")
            except Exception:
                pass
            try:
                self._serial.close()
            finally:
                self._serial = None

    def _write(self, command: str) -> None:
        if self._serial is None:
            raise CommunicationError("USB is not connected")
        self._serial.write((command + "\n").encode("ascii"))
        self._serial.flush()

    def read_sample(self) -> Sample | None:
        if self._serial is None:
            raise CommunicationError("USB is not connected")
        raw = self._serial.readline()
        if not raw:
            return None
        text = raw.decode("utf-8", errors="replace").strip()
        if text.startswith("S,"):
            try:
                return Sample.from_compact_usb(text)
            except ValueError:
                return None
        try:
            payload = json.loads(text)
        except (json.JSONDecodeError, ValueError):
            return None
        if payload.get("type") != "sample" and not ({"f1", "f2"} <= payload.keys()):
            return None
        return Sample.from_payload(payload)

    def move(self, direction: str, steps: int, delay_us: int) -> None:
        self._write(f"MOVE,{direction.upper()},{steps},{delay_us}")

    def stop(self) -> None:
        self._write("STOP")

    def set_recording(self, active: bool) -> None:
        self._write(f"RECORD,{1 if active else 0}")


class WifiTransport(Transport):
    name = "Wi-Fi"

    def __init__(self, base_url: str) -> None:
        self.base_url = base_url.rstrip("/")
        self._session: Any = None

    def connect(self) -> None:
        try:
            import requests

            self._session = requests.Session()
            response = self._session.get(f"{self.base_url}/sensors", timeout=2)
            response.raise_for_status()
            Sample.from_payload(response.json())
        except Exception as exc:
            self.close()
            raise CommunicationError(f"Could not reach ESP32 at {self.base_url}: {exc}") from exc

    def close(self) -> None:
        if self._session is not None:
            self._session.close()
            self._session = None

    def _get(self, path: str, **params: Any) -> Any:
        if self._session is None:
            raise CommunicationError("Wi-Fi is not connected")
        response = self._session.get(f"{self.base_url}{path}", params=params, timeout=1.5)
        response.raise_for_status()
        return response

    def read_sample(self) -> Sample | None:
        return Sample.from_payload(self._get("/sensors").json())

    def move(self, direction: str, steps: int, delay_us: int) -> None:
        firmware_direction = "forward" if direction.lower() == "up" else "backward"
        self._get("/move", dir=firmware_direction, steps=steps, speed=delay_us)

    def stop(self) -> None:
        self._get("/stop")

    def set_recording(self, active: bool) -> None:
        # High-frequency capture is intentionally USB-only.
        return None


class SensorWorker:
    """Owns the transport on a background thread and reports events to the UI."""

    def __init__(self, on_event: Callable[[str, Any], None]) -> None:
        self.on_event = on_event
        self._commands: queue.Queue[tuple[str, tuple[Any, ...]]] = queue.Queue()
        self._thread: threading.Thread | None = None
        self._stop_event = threading.Event()
        self.transport: Transport | None = None

    @property
    def running(self) -> bool:
        return self._thread is not None and self._thread.is_alive()

    def start(self, transport: Transport) -> None:
        self.shutdown()
        self.transport = transport
        self._stop_event.clear()
        self._thread = threading.Thread(target=self._run, name="esp32-worker", daemon=True)
        self._thread.start()

    def send_move(self, direction: str, steps: int, delay_us: int) -> None:
        self._commands.put(("move", (direction, steps, delay_us)))

    def send_stop(self) -> None:
        self._commands.put(("stop", ()))

    def set_recording(self, active: bool) -> None:
        self._commands.put(("record", (active,)))

    def shutdown(self) -> None:
        self._stop_event.set()
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2)
        if self.transport:
            self.transport.close()
        self._thread = None
        self.transport = None
        while not self._commands.empty():
            try:
                self._commands.get_nowait()
            except queue.Empty:
                break

    def _run(self) -> None:
        assert self.transport is not None
        try:
            self.transport.connect()
            self.on_event("connected", self.transport.name)
            while not self._stop_event.is_set():
                self._process_commands()
                sample = self.transport.read_sample()
                if sample is not None:
                    self.on_event("sample", sample)
                if isinstance(self.transport, WifiTransport):
                    self._stop_event.wait(0.18)
        except Exception as exc:
            if not self._stop_event.is_set():
                self.on_event("error", str(exc))
        finally:
            self.transport.close()
            self.on_event("disconnected", None)

    def _process_commands(self) -> None:
        assert self.transport is not None
        while True:
            try:
                command, args = self._commands.get_nowait()
            except queue.Empty:
                return
            if command == "move":
                self.transport.move(*args)
                self.on_event("command", f"Moving {args[0]}: {args[1]} steps")
            elif command == "stop":
                self.transport.stop()
                self.on_event("command", "Motor stopped")
            elif command == "record":
                self.transport.set_recording(bool(args[0]))
                state = "High-rate recording started" if args[0] else "High-rate recording stopped"
                self.on_event("recording_state", state)


def available_serial_ports() -> list[tuple[str, str]]:
    try:
        from serial.tools import list_ports

        return [(port.device, port.description) for port in list_ports.comports()]
    except ImportError:
        return []
