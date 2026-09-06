"""Idempotency ledger.

Records which Jira worklogs have already been handed to a sink and what they
looked like at the time, so a re-run is a no-op, an edit in Jira is re-exported,
and a deletion in Jira is reported instead of silently diverging.
"""

from __future__ import annotations

import sqlite3
from datetime import date, datetime, timezone
from pathlib import Path
from typing import Iterable, Sequence

from .models import WorkLog

SCHEMA = """
CREATE TABLE IF NOT EXISTS synced_worklog (
    worklog_id   TEXT PRIMARY KEY,
    content_hash TEXT NOT NULL,
    started_on   TEXT NOT NULL,
    user         TEXT NOT NULL,
    sink_ref     TEXT NOT NULL,
    synced_at    TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_synced_worklog_started ON synced_worklog(started_on);
"""


class Ledger:
    def __init__(self, path: str | Path):
        self.path = Path(path)
        if self.path.parent and str(self.path.parent) not in ("", "."):
            self.path.parent.mkdir(parents=True, exist_ok=True)
        self.connection = sqlite3.connect(self.path)
        self.connection.row_factory = sqlite3.Row
        self.connection.executescript(SCHEMA)
        self.connection.commit()

    def close(self) -> None:
        self.connection.close()

    def __enter__(self) -> "Ledger":
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    def known_hashes(self) -> dict[str, str]:
        rows = self.connection.execute("SELECT worklog_id, content_hash FROM synced_worklog")
        return {row["worklog_id"]: row["content_hash"] for row in rows}

    def ids_in_range(self, since: date, until: date) -> set[str]:
        rows = self.connection.execute(
            "SELECT worklog_id FROM synced_worklog WHERE started_on BETWEEN ? AND ?",
            (since.isoformat(), until.isoformat()),
        )
        return {row["worklog_id"] for row in rows}

    def select_changed(self, worklogs: Iterable[WorkLog]) -> tuple[list[WorkLog], int]:
        """Split incoming worklogs into those needing export and a count of the rest."""
        known = self.known_hashes()
        changed: list[WorkLog] = []
        unchanged = 0
        for worklog in worklogs:
            if known.get(worklog.id) == worklog.content_hash():
                unchanged += 1
            else:
                changed.append(worklog)
        return changed, unchanged

    def find_removed(self, since: date, until: date, present_ids: Iterable[str]) -> list[str]:
        """Worklogs we exported for this window that Jira no longer reports."""
        return sorted(self.ids_in_range(since, until) - set(present_ids))

    def record(self, worklogs: Sequence[WorkLog], sink_ref: str) -> None:
        now = datetime.now(timezone.utc).isoformat(timespec="seconds")
        self.connection.executemany(
            """
            INSERT INTO synced_worklog (worklog_id, content_hash, started_on, user, sink_ref, synced_at)
            VALUES (?, ?, ?, ?, ?, ?)
            ON CONFLICT(worklog_id) DO UPDATE SET
                content_hash = excluded.content_hash,
                started_on   = excluded.started_on,
                user         = excluded.user,
                sink_ref     = excluded.sink_ref,
                synced_at    = excluded.synced_at
            """,
            [
                (
                    w.id,
                    w.content_hash(),
                    w.started_on.isoformat(),
                    w.author_email or w.author_display_name,
                    sink_ref,
                    now,
                )
                for w in worklogs
            ],
        )
        self.connection.commit()

    def forget(self, worklog_ids: Iterable[str]) -> None:
        self.connection.executemany(
            "DELETE FROM synced_worklog WHERE worklog_id = ?", [(i,) for i in worklog_ids]
        )
        self.connection.commit()
