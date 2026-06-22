from __future__ import annotations

import csv
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from typing import Iterable


@dataclass(frozen=True)
class Measurement:
    time_ms: int
    fsr1: int
    fsr2: int


FIELDNAMES = list(Measurement.__dataclass_fields__)


@dataclass(frozen=True)
class ResponseDelay:
    fsr1_onset_s: float
    fsr2_onset_s: float
    signed_delay_ms: float

    @property
    def summary(self) -> str:
        magnitude = abs(self.signed_delay_ms)
        if magnitude < 0.5:
            return "Response delay: both sensors responded together"
        leader = "FSR 1" if self.signed_delay_ms > 0 else "FSR 2"
        return f"Response delay: {leader} leads by {magnitude:.0f} ms"


def calculate_response_delay(measurements: Iterable[Measurement]) -> ResponseDelay | None:
    """Estimate onset lag using each channel's first 10%-of-range crossing."""
    rows = list(measurements)
    if len(rows) < 3:
        return None
    fsr1 = [row.fsr1 for row in rows]
    fsr2 = [row.fsr2 for row in rows]
    range1 = max(fsr1) - min(fsr1)
    range2 = max(fsr2) - min(fsr2)
    if range1 < 20 or range2 < 20:
        return None
    threshold1 = min(fsr1) + 0.10 * range1
    threshold2 = min(fsr2) + 0.10 * range2
    onset1 = next(row.time_ms / 1000 for row in rows if row.fsr1 >= threshold1)
    onset2 = next(row.time_ms / 1000 for row in rows if row.fsr2 >= threshold2)
    return ResponseDelay(onset1, onset2, (onset2 - onset1) * 1000)


def export_csv(path: str | Path, measurements: Iterable[Measurement]) -> None:
    rows = list(measurements)
    with Path(path).open("w", newline="", encoding="utf-8-sig") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDNAMES)
        writer.writeheader()
        writer.writerows(asdict(row) for row in rows)


def export_xlsx(path: str | Path, measurements: Iterable[Measurement]) -> None:
    from openpyxl import Workbook
    from openpyxl.chart import LineChart, Reference
    from openpyxl.styles import Alignment, Font, PatternFill
    from openpyxl.utils import get_column_letter

    rows = list(measurements)
    workbook = Workbook()
    sheet = workbook.active
    sheet.title = "Measurements"
    display_headers = [name.replace("_", " ").title() for name in FIELDNAMES]
    sheet.append(display_headers)
    for item in rows:
        sheet.append(list(asdict(item).values()))

    header_fill = PatternFill("solid", fgColor="17365D")
    for cell in sheet[1]:
        cell.fill = header_fill
        cell.font = Font(color="FFFFFF", bold=True)
        cell.alignment = Alignment(horizontal="center")
    sheet.freeze_panes = "A2"
    sheet.auto_filter.ref = sheet.dimensions
    widths = [14, 12, 12]
    for index, width in enumerate(widths, start=1):
        sheet.column_dimensions[get_column_letter(index)].width = width
    for row in sheet.iter_rows(min_row=2, min_col=1, max_col=1):
        row[0].number_format = "0"

    summary = workbook.create_sheet("Summary")
    summary["A1"] = "ESP32 Sensor Test Summary"
    summary["A1"].font = Font(size=16, bold=True, color="17365D")
    metadata = [
        ("Exported", datetime.now().astimezone().isoformat(timespec="seconds")),
        ("Samples", len(rows)),
        ("Duration (s)", rows[-1].time_ms / 1000 if rows else 0),
        ("Maximum FSR 1", max((row.fsr1 for row in rows), default=0)),
        ("Maximum FSR 2", max((row.fsr2 for row in rows), default=0)),
    ]
    for row_index, (label, value) in enumerate(metadata, start=3):
        summary.cell(row_index, 1, label).font = Font(bold=True)
        summary.cell(row_index, 2, value)
    summary.column_dimensions["A"].width = 22
    summary.column_dimensions["B"].width = 28

    if rows:
        chart = LineChart()
        chart.title = "FSR Measurements"
        chart.y_axis.title = "Raw ADC value"
        chart.x_axis.title = "Elapsed time (s)"
        chart.style = 13
        chart.height = 10
        chart.width = 19
        data = Reference(sheet, min_col=2, max_col=3, min_row=1, max_row=len(rows) + 1)
        categories = Reference(sheet, min_col=1, min_row=2, max_row=len(rows) + 1)
        chart.add_data(data, titles_from_data=True)
        chart.set_categories(categories)
        summary.add_chart(chart, "D3")

    workbook.save(path)
