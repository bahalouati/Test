"""CSV export sink.

The path that works without any Sciforma API access: produce one import-ready
file per person, plus a combined file, and let people upload them. Column names
live in config because every Sciforma installation names its import columns
slightly differently.
"""

from __future__ import annotations

import csv
import re
from datetime import date
from pathlib import Path
from typing import Sequence

from ..models import TimesheetLine
from .base import TimesheetSink

DEFAULT_COLUMNS = {
    "resource": "Resource",
    "date": "Date",
    "project": "ProjectCode",
    "task": "TaskCode",
    "hours": "Hours",
    "comment": "Comment",
}


def _slug(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9._-]+", "_", value).strip("_") or "unknown"


class CsvSink(TimesheetSink):
    def __init__(
        self,
        output_dir: str = "out",
        columns: dict[str, str] | None = None,
        date_format: str = "%Y-%m-%d",
        split_per_user: bool = True,
        delimiter: str = ",",
    ):
        self.output_dir = Path(output_dir)
        self.columns = {**DEFAULT_COLUMNS, **(columns or {})}
        self.date_format = date_format
        self.split_per_user = split_per_user
        self.delimiter = delimiter

    def _row(self, line: TimesheetLine) -> dict[str, str]:
        return {
            self.columns["resource"]: line.user,
            self.columns["date"]: line.day.strftime(self.date_format),
            self.columns["project"]: line.project_code,
            self.columns["task"]: line.task_code,
            self.columns["hours"]: f"{line.hours:.2f}",
            self.columns["comment"]: line.comment,
        }

    def _write_file(self, path: Path, lines: Sequence[TimesheetLine]) -> str:
        path.parent.mkdir(parents=True, exist_ok=True)
        header = [self.columns[key] for key in ("resource", "date", "project", "task", "hours", "comment")]
        with path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=header, delimiter=self.delimiter)
            writer.writeheader()
            for line in lines:
                writer.writerow(self._row(line))
        return str(path)

    def write(self, lines: Sequence[TimesheetLine], since: date, until: date) -> list[str]:
        if not lines:
            return []
        window = f"{since.isoformat()}_{until.isoformat()}"
        ordered = sorted(lines, key=lambda l: (l.user, l.day, l.project_code, l.task_code))
        outputs = [self._write_file(self.output_dir / f"timesheet_{window}.csv", ordered)]

        if self.split_per_user:
            per_user: dict[str, list[TimesheetLine]] = {}
            for line in ordered:
                per_user.setdefault(line.user, []).append(line)
            for user, user_lines in sorted(per_user.items()):
                outputs.append(
                    self._write_file(
                        self.output_dir / window / f"{_slug(user)}.csv", user_lines
                    )
                )
        return outputs
