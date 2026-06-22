from __future__ import annotations

import queue
import time
import tkinter as tk
from collections import deque
from datetime import datetime
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure

from .communications import (
    Sample,
    SensorWorker,
    SerialTransport,
    WifiTransport,
    available_serial_ports,
)
from .data_export import Measurement, calculate_response_delay, export_csv, export_xlsx


class SensorTesterApp(tk.Tk):
    GRAPH_WINDOW_SECONDS = 30.0

    def __init__(self) -> None:
        super().__init__()
        self.title("ESP32 Sensor Tester")
        self.geometry("1120x720")
        self.minsize(900, 620)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

        self.events: queue.Queue[tuple[str, object]] = queue.Queue()
        self.worker = SensorWorker(lambda event, data: self.events.put((event, data)))
        self.measurements: list[Measurement] = []
        self.graph_time: deque[float] = deque()
        self.graph_fsr1: deque[int] = deque()
        self.graph_fsr2: deque[int] = deque()
        self.recording = False
        self.recording_started = 0.0
        self.recording_device_started_us: int | None = None
        self.current_recording_start_index = 0
        self.last_graph_sample_us: int | None = None
        self.last_graph_host_time = 0.0
        self.connected_transport = ""
        self.current_direction = ""
        self.port_labels: dict[str, str] = {}

        self._make_variables()
        self._build_ui()
        self._refresh_ports()
        self.after(50, self._drain_events)

    def _make_variables(self) -> None:
        self.mode_var = tk.StringVar(value="usb")
        self.port_var = tk.StringVar()
        self.wifi_var = tk.StringVar(value="http://192.168.4.1")
        self.connection_var = tk.StringVar(value="Disconnected")
        self.test_var = tk.StringVar(value="Compression")
        self.steps_var = tk.IntVar(value=200)
        self.delay_var = tk.IntVar(value=500)
        self.fsr1_var = tk.StringVar(value="—")
        self.fsr2_var = tk.StringVar(value="—")
        self.motion_var = tk.StringVar(value="Idle")
        self.record_var = tk.StringVar(value="Not recording")
        self.response_delay_var = tk.StringVar(value="Response delay: not calculated")

    def _build_ui(self) -> None:
        style = ttk.Style(self)
        style.configure("Title.TLabel", font=("TkDefaultFont", 18, "bold"))
        style.configure("Reading.TLabel", font=("TkDefaultFont", 22, "bold"))
        style.configure("Emergency.TButton", font=("TkDefaultFont", 13, "bold"))

        outer = ttk.Frame(self, padding=14)
        outer.pack(fill="both", expand=True)
        ttk.Label(outer, text="ESP32 Sensor Tester", style="Title.TLabel").pack(anchor="w")
        body = ttk.Frame(outer)
        body.pack(fill="both", expand=True, pady=(12, 0))
        controls = ttk.Frame(body, width=320)
        controls.pack(side="left", fill="y", padx=(0, 14))
        plot_area = ttk.Frame(body)
        plot_area.pack(side="left", fill="both", expand=True)

        connection = ttk.LabelFrame(controls, text="Connection", padding=10)
        connection.pack(fill="x")
        mode_line = ttk.Frame(connection)
        mode_line.pack(fill="x")
        ttk.Radiobutton(mode_line, text="USB", variable=self.mode_var, value="usb", command=self._mode_changed).pack(side="left")
        ttk.Radiobutton(mode_line, text="Wi-Fi", variable=self.mode_var, value="wifi", command=self._mode_changed).pack(side="left", padx=12)
        self.port_combo = ttk.Combobox(connection, textvariable=self.port_var, state="readonly", width=30)
        self.port_combo.pack(fill="x", pady=(8, 4))
        self.refresh_button = ttk.Button(connection, text="Refresh ports", command=self._refresh_ports)
        self.refresh_button.pack(fill="x")
        self.wifi_entry = ttk.Entry(connection, textvariable=self.wifi_var)
        connect_line = ttk.Frame(connection)
        connect_line.pack(fill="x", pady=(8, 0))
        self.connect_button = ttk.Button(connect_line, text="Connect", command=self._toggle_connection)
        self.connect_button.pack(side="left", fill="x", expand=True)
        ttk.Label(connect_line, textvariable=self.connection_var).pack(side="left", padx=(10, 0))

        test = ttk.LabelFrame(controls, text="Test setup", padding=10)
        test.pack(fill="x", pady=(12, 0))
        ttk.Label(test, text="Test name/type").pack(anchor="w")
        ttk.Combobox(
            test,
            textvariable=self.test_var,
            values=("Compression", "Tension", "Cyclic", "Manual"),
        ).pack(fill="x", pady=(2, 8))
        self._labeled_spinbox(test, "Motor steps", self.steps_var, 1, 10_000_000)
        self._labeled_spinbox(test, "Pulse delay (µs) — lower is faster", self.delay_var, 20, 5000)
        direction_line = ttk.Frame(test)
        direction_line.pack(fill="x", pady=(10, 4))
        ttk.Button(direction_line, text="▲ Move up", command=lambda: self._move("up")).pack(side="left", fill="x", expand=True, padx=(0, 4))
        ttk.Button(direction_line, text="▼ Move down", command=lambda: self._move("down")).pack(side="left", fill="x", expand=True, padx=(4, 0))
        ttk.Button(test, text="STOP MOTOR", style="Emergency.TButton", command=self._stop_motor).pack(fill="x", pady=(6, 0), ipady=7)
        ttk.Label(test, textvariable=self.motion_var).pack(anchor="center", pady=(6, 0))

        recording = ttk.LabelFrame(controls, text="Recording", padding=10)
        recording.pack(fill="x", pady=(12, 0))
        record_line = ttk.Frame(recording)
        record_line.pack(fill="x")
        self.record_button = ttk.Button(record_line, text="Start recording", command=self._toggle_recording)
        self.record_button.pack(side="left", fill="x", expand=True)
        ttk.Button(record_line, text="Clear", command=self._clear_data).pack(side="left", padx=(6, 0))
        ttk.Label(recording, textvariable=self.record_var).pack(anchor="w", pady=(5, 6))
        ttk.Label(recording, textvariable=self.response_delay_var, wraplength=285).pack(anchor="w", pady=(0, 6))
        export_line = ttk.Frame(recording)
        export_line.pack(fill="x")
        ttk.Button(export_line, text="Export CSV", command=lambda: self._export("csv")).pack(side="left", fill="x", expand=True, padx=(0, 3))
        ttk.Button(export_line, text="Export Excel", command=lambda: self._export("xlsx")).pack(side="left", fill="x", expand=True, padx=(3, 0))

        readings = ttk.Frame(plot_area)
        readings.pack(fill="x")
        self._reading_card(readings, "FSR 1 (GPIO 32)", self.fsr1_var).pack(side="left", fill="x", expand=True, padx=(0, 5))
        self._reading_card(readings, "FSR 2 (GPIO 33)", self.fsr2_var).pack(side="left", fill="x", expand=True, padx=(5, 0))

        figure = Figure(figsize=(7, 5), dpi=100, constrained_layout=True)
        self.axes = figure.add_subplot(111)
        self.axes.set_title("Live sensor measurements")
        self.axes.set_xlabel("Seconds ago (0 = now)")
        self.axes.set_ylabel("Raw ADC value")
        self.axes.set_ylim(0, 4095)
        self.axes.set_xlim(-self.GRAPH_WINDOW_SECONDS, 0)
        self.axes.grid(True, alpha=0.25)
        (self.line1,) = self.axes.plot([], [], color="#00a88f", label="FSR 1")
        (self.line2,) = self.axes.plot([], [], color="#e23b62", label="FSR 2")
        self.axes.legend(loc="upper left")
        self.canvas = FigureCanvasTkAgg(figure, master=plot_area)
        self.canvas.get_tk_widget().pack(fill="both", expand=True, pady=(10, 0))
        self._mode_changed()

    def _labeled_spinbox(self, parent: ttk.Frame, text: str, variable: tk.Variable, low: int, high: int) -> None:
        ttk.Label(parent, text=text).pack(anchor="w")
        ttk.Spinbox(parent, textvariable=variable, from_=low, to=high).pack(fill="x", pady=(2, 8))

    def _reading_card(self, parent: ttk.Frame, title: str, variable: tk.StringVar) -> ttk.Frame:
        card = ttk.LabelFrame(parent, text=title, padding=12)
        ttk.Label(card, textvariable=variable, style="Reading.TLabel").pack()
        ttk.Label(card, text="raw ADC counts").pack()
        return card

    def _mode_changed(self) -> None:
        usb = self.mode_var.get() == "usb"
        if usb:
            self.wifi_entry.pack_forget()
            self.port_combo.pack(fill="x", pady=(8, 4), before=self.refresh_button)
            self.refresh_button.pack(fill="x")
        else:
            self.port_combo.pack_forget()
            self.refresh_button.pack_forget()
            self.wifi_entry.pack(fill="x", pady=(8, 0))

    def _refresh_ports(self) -> None:
        ports = available_serial_ports()
        self.port_labels = {f"{device} — {description}": device for device, description in ports}
        labels = list(self.port_labels)
        self.port_combo["values"] = labels
        if labels:
            self.port_var.set(labels[0])
        else:
            self.port_var.set("")

    def _toggle_connection(self) -> None:
        if self.worker.running:
            self.worker.shutdown()
            self.connection_var.set("Disconnected")
            self.connect_button.configure(text="Connect")
            return
        if self.mode_var.get() == "usb":
            port = self.port_labels.get(self.port_var.get(), self.port_var.get())
            if not port:
                messagebox.showwarning("No serial port", "Connect the ESP32, then click Refresh ports.")
                return
            transport = SerialTransport(port)
        else:
            url = self.wifi_var.get().strip()
            if not url:
                messagebox.showwarning("No Wi-Fi address", "Enter the ESP32 address, normally http://192.168.4.1")
                return
            transport = WifiTransport(url)
        self.connection_var.set("Connecting…")
        self.connect_button.configure(text="Disconnect")
        self.worker.start(transport)

    def _move(self, direction: str) -> None:
        if not self.worker.running:
            messagebox.showwarning("Not connected", "Connect to the ESP32 first.")
            return
        try:
            steps = int(self.steps_var.get())
            delay = int(self.delay_var.get())
            if steps < 1 or not 20 <= delay <= 5000:
                raise ValueError
        except (tk.TclError, ValueError):
            messagebox.showerror("Invalid motor settings", "Steps must be at least 1 and delay must be 20–5000 µs.")
            return
        self.current_direction = direction
        self.worker.send_move(direction, steps, delay)
        self.motion_var.set(f"Commanded {direction}: {steps} steps")

    def _stop_motor(self) -> None:
        if self.worker.running:
            self.worker.send_stop()
        self.motion_var.set("Stopped")

    def _toggle_recording(self) -> None:
        if not self.recording:
            if not self.worker.running:
                messagebox.showwarning("Not connected", "Connect to the ESP32 before recording.")
                return
            self.recording = True
            self.recording_started = time.monotonic()
            self.recording_device_started_us = None
            self.current_recording_start_index = len(self.measurements)
            self.response_delay_var.set("Response delay: recording…")
            self.worker.set_recording(True)
            self.record_button.configure(text="Stop recording")
            self.record_var.set(f"Recording… {len(self.measurements)} samples")
        else:
            self.recording = False
            self.worker.set_recording(False)
            self.record_button.configure(text="Start recording")
            self.record_var.set(f"Stopped — {len(self.measurements)} samples")
            latest_recording = self.measurements[self.current_recording_start_index :]
            delay = calculate_response_delay(latest_recording)
            self.response_delay_var.set(
                delay.summary if delay else "Response delay: no clear force change detected"
            )

    def _clear_data(self) -> None:
        if self.recording:
            messagebox.showwarning("Recording active", "Stop recording before clearing data.")
            return
        self.measurements.clear()
        self.record_var.set("Not recording — 0 samples")
        self.response_delay_var.set("Response delay: not calculated")

    def _export(self, kind: str) -> None:
        if not self.measurements:
            messagebox.showinfo("No data", "Record some measurements before exporting.")
            return
        test_name = "".join(c if c.isalnum() or c in "-_" else "_" for c in self.test_var.get()).strip("_") or "test"
        default = f"{test_name}_{datetime.now():%Y%m%d_%H%M%S}.{kind}"
        path = filedialog.asksaveasfilename(
            title=f"Export {kind.upper()}",
            defaultextension=f".{kind}",
            initialfile=default,
            filetypes=[("CSV files", "*.csv")] if kind == "csv" else [("Excel files", "*.xlsx")],
        )
        if not path:
            return
        try:
            if kind == "csv":
                export_csv(path, self.measurements)
            else:
                export_xlsx(path, self.measurements)
            messagebox.showinfo("Export complete", f"Saved {len(self.measurements)} measurements to:\n{Path(path)}")
        except Exception as exc:
            messagebox.showerror("Export failed", str(exc))

    def _drain_events(self) -> None:
        redraw = False
        while True:
            try:
                event, data = self.events.get_nowait()
            except queue.Empty:
                break
            if event == "connected":
                self.connected_transport = str(data)
                self.connection_var.set(f"Connected ({data})")
            elif event == "disconnected":
                if self.connection_var.get().startswith("Connected"):
                    self.connection_var.set("Disconnected")
                self.connect_button.configure(text="Connect")
            elif event == "error":
                self.connection_var.set("Connection error")
                self.connect_button.configure(text="Connect")
                messagebox.showerror("ESP32 communication error", str(data))
            elif event == "command":
                self.motion_var.set(str(data))
            elif event == "recording_state":
                pass
            elif event == "sample":
                redraw = self._accept_sample(data) or redraw  # type: ignore[arg-type]
        if redraw:
            self._redraw_plot()
        self.after(50, self._drain_events)

    def _accept_sample(self, sample: Sample) -> bool:
        now = time.monotonic()
        sample_clock = sample.device_time_us / 1_000_000 if sample.device_time_us else now
        if self.recording:
            if sample.device_time_us:
                if self.recording_device_started_us is None:
                    self.recording_device_started_us = sample.device_time_us
                elapsed = (sample.device_time_us - self.recording_device_started_us) / 1_000_000
            else:
                elapsed = now - self.recording_started
            self.measurements.append(
                Measurement(
                    time_ms=round(elapsed * 1000),
                    fsr1=sample.fsr1_raw,
                    fsr2=sample.fsr2_raw,
                )
            )
        if sample.device_time_us:
            plot_due = (
                self.last_graph_sample_us is None
                or sample.device_time_us - self.last_graph_sample_us >= 200_000
            )
        else:
            plot_due = now - self.last_graph_host_time >= 0.2
        if not plot_due:
            return False

        self.last_graph_sample_us = sample.device_time_us or self.last_graph_sample_us
        self.last_graph_host_time = now
        self.fsr1_var.set(str(sample.fsr1_raw))
        self.fsr2_var.set(str(sample.fsr2_raw))
        if sample.motion_status_known:
            self.motion_var.set(
                f"Moving {self.current_direction} — {sample.steps_remaining} steps left"
                if sample.moving
                else "Idle"
            )
        self.graph_time.append(sample_clock)
        self.graph_fsr1.append(sample.fsr1_raw)
        self.graph_fsr2.append(sample.fsr2_raw)
        cutoff = sample_clock - self.GRAPH_WINDOW_SECONDS
        while len(self.graph_time) > 1 and self.graph_time[0] < cutoff:
            self.graph_time.popleft()
            self.graph_fsr1.popleft()
            self.graph_fsr2.popleft()
        if self.recording:
            recorded_count = len(self.measurements) - self.current_recording_start_index
            self.record_var.set(f"Recording… {recorded_count} high-rate samples")
        return True

    def _redraw_plot(self) -> None:
        if not self.graph_time:
            return
        newest = self.graph_time[-1]
        point_count = len(self.graph_time)
        stride = max(1, point_count // 2000)
        relative_time = [sample_time - newest for sample_time in list(self.graph_time)[::stride]]
        self.line1.set_data(relative_time, list(self.graph_fsr1)[::stride])
        self.line2.set_data(relative_time, list(self.graph_fsr2)[::stride])
        self.canvas.draw_idle()

    def _on_close(self) -> None:
        if self.worker.running:
            self.worker.send_stop()
            time.sleep(0.05)
        self.worker.shutdown()
        self.destroy()


def main() -> None:
    SensorTesterApp().mainloop()


if __name__ == "__main__":
    main()
