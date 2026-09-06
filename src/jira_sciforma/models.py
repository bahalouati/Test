"""Core data types passed between the source, mapping, aggregation and sink layers."""

from __future__ import annotations

import hashlib
from dataclasses import dataclass, field
from datetime import date
from typing import Sequence


@dataclass(frozen=True)
class Issue:
    """The Jira issue a worklog hangs off, reduced to the fields mapping rules can match on."""

    key: str
    project_key: str
    summary: str
    issue_type: str
    labels: tuple[str, ...] = ()
    components: tuple[str, ...] = ()
    parent_key: str | None = None


@dataclass(frozen=True)
class WorkLog:
    """One Jira worklog entry, normalised away from Jira's JSON shape."""

    id: str
    issue: Issue
    author_account_id: str
    author_display_name: str
    author_email: str | None
    started_on: date
    seconds: int
    comment: str
    updated: str

    def content_hash(self) -> str:
        """Fingerprint of the fields that matter downstream.

        The ledger compares this to spot worklogs that were edited in Jira after
        they were already exported.
        """
        parts = [
            self.issue.key,
            self.author_account_id,
            self.started_on.isoformat(),
            str(self.seconds),
            self.comment,
        ]
        return hashlib.sha256("\x1f".join(parts).encode("utf-8")).hexdigest()[:16]


@dataclass(frozen=True)
class MappedWorkLog:
    """A worklog after a mapping rule has resolved its Sciforma destination."""

    worklog: WorkLog
    project_code: str
    task_code: str
    rule_name: str


@dataclass(frozen=True)
class TimesheetLine:
    """One row as Sciforma wants it: a person, a day, a task, an hour figure."""

    user: str
    day: date
    project_code: str
    task_code: str
    hours: float
    comment: str
    worklog_ids: tuple[str, ...] = field(default=())


@dataclass
class SyncReport:
    """What a run did, for the CLI to print and for the caller to assert on."""

    lines: Sequence[TimesheetLine] = ()
    unmapped: Sequence[WorkLog] = ()
    skipped_unchanged: int = 0
    removed_worklog_ids: Sequence[str] = ()
    outputs: Sequence[str] = ()
    dry_run: bool = False

    @property
    def total_hours(self) -> float:
        return round(sum(line.hours for line in self.lines), 2)
