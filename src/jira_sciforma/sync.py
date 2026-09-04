"""The pipeline: fetch, filter against the ledger, map, aggregate, write."""

from __future__ import annotations

import logging
from datetime import date
from typing import Protocol, Sequence

from .aggregate import build_timesheet
from .config import Config
from .ledger import Ledger
from .mapping import MappingTable
from .models import SyncReport, WorkLog
from .sinks.base import TimesheetSink

log = logging.getLogger(__name__)


class WorklogSource(Protocol):
    def fetch_worklogs(self, since: date, until: date) -> list[WorkLog]: ...


def run_sync(
    config: Config,
    source: WorklogSource,
    mapping: MappingTable,
    sink: TimesheetSink,
    ledger: Ledger,
    since: date,
    until: date,
    *,
    dry_run: bool = False,
    full_resync: bool = False,
) -> SyncReport:
    worklogs = source.fetch_worklogs(since, until)

    removed = ledger.find_removed(since, until, [w.id for w in worklogs])

    if full_resync:
        candidates, unchanged = list(worklogs), 0
    else:
        candidates, unchanged = ledger.select_changed(worklogs)
    log.info("%d worklog(s) to export, %d unchanged", len(candidates), unchanged)

    mapped, unmapped = mapping.apply(candidates)
    if unmapped:
        log.warning("%d worklog(s) matched no mapping rule and were left out", len(unmapped))

    lines = build_timesheet(mapped, config.rounding)

    outputs: Sequence[str] = []
    if lines and not dry_run:
        outputs = sink.write(lines, since, until)
        ledger.record([item.worklog for item in mapped], sink_ref=";".join(outputs) or sink.__class__.__name__)

    return SyncReport(
        lines=lines,
        unmapped=unmapped,
        skipped_unchanged=unchanged,
        removed_worklog_ids=removed,
        outputs=outputs,
        dry_run=dry_run,
    )
