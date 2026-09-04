"""The write side of the sync, kept behind one narrow interface.

Everything upstream of this produces ``TimesheetLine`` objects and knows nothing
about how they reach Sciforma. Swapping the CSV export for a REST client is
therefore a config change, not a rewrite.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from datetime import date
from typing import Sequence

from .. import models


class TimesheetSink(ABC):
    """Accepts timesheet lines and returns a human-readable reference per output."""

    @abstractmethod
    def write(
        self, lines: Sequence[models.TimesheetLine], since: date, until: date
    ) -> list[str]:
        """Deliver the lines and return references (file paths, ids, URLs)."""

    def describe_removals(self, worklog_ids: Sequence[str]) -> str | None:
        """Optional hook for reporting entries that disappeared from Jira."""
        if not worklog_ids:
            return None
        return f"{len(worklog_ids)} previously exported worklog(s) no longer exist in Jira"


def get_sink(kind: str, output_dir: str, options: dict) -> TimesheetSink:
    """Factory used by the CLI so sinks are selected by config string."""
    from .csv_sink import CsvSink
    from .sciforma_rest import SciformaRestSink

    if kind == "csv":
        return CsvSink(output_dir=output_dir, **options)
    if kind == "sciforma_rest":
        return SciformaRestSink(**options)
    raise ValueError(f"unknown sink kind: {kind!r} (expected 'csv' or 'sciforma_rest')")
