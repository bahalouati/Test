"""Turn mapped worklogs into the daily timesheet lines Sciforma expects.

Jira records time as arbitrary per-worklog seconds; Sciforma wants one hour
figure per person, per day, per task. This module does that collapse and the
rounding that goes with it.
"""

from __future__ import annotations

from collections import defaultdict
from datetime import date
from decimal import ROUND_HALF_UP, Decimal
from typing import Iterable, Sequence

from .config import RoundingConfig
from .models import MappedWorkLog, TimesheetLine, WorkLog

COMMENT_MAX_LENGTH = 255


def user_key(worklog: WorkLog) -> str:
    """Identify the person. Email is the stable join key onto a Sciforma resource;
    display name is only a fallback for instances that hide email addresses."""
    return worklog.author_email or worklog.author_display_name


def _round_half_up(value: Decimal) -> int:
    return int(value.quantize(Decimal("1"), rounding=ROUND_HALF_UP))


def allocate_units(seconds: Sequence[int], increment_seconds: int) -> list[int]:
    """Split a day's seconds into whole rounding units without losing the total.

    Largest-remainder allocation: every line takes its floor, then the units
    left over by rounding the *day* total go to the lines with the biggest
    remainders. The result always sums to the rounded day total, so a person's
    day still adds up after rounding.
    """
    if increment_seconds <= 0:
        raise ValueError("increment_seconds must be positive")
    if not seconds:
        return []

    total_units = _round_half_up(Decimal(sum(seconds)) / Decimal(increment_seconds))
    units = [value // increment_seconds for value in seconds]
    remainders = [value % increment_seconds for value in seconds]

    leftover = total_units - sum(units)
    order = sorted(range(len(seconds)), key=lambda i: (-remainders[i], -seconds[i], i))
    for index in order[:leftover]:
        units[index] += 1

    _lift_zeroed_lines(units, seconds, total_units)
    return units


def _lift_zeroed_lines(units: list[int], seconds: Sequence[int], total_units: int) -> None:
    """Give sub-increment entries a unit by borrowing from the largest lines.

    Without this a 5-minute worklog rounds to nothing and its time silently
    lands on whatever else that person booked that day. Only possible when the
    day has at least as many units as it has non-empty lines; otherwise the
    entries genuinely do not fit and the largest-remainder result stands.
    """
    needy = [i for i, value in enumerate(seconds) if value > 0 and units[i] == 0]
    if not needy or total_units < sum(1 for value in seconds if value > 0):
        return
    for index in needy:
        donor = max(range(len(units)), key=lambda i: (units[i], seconds[i]))
        if units[donor] <= 1:
            return
        units[donor] -= 1
        units[index] += 1


def _build_comment(worklogs: Sequence[WorkLog]) -> str:
    """One line of provenance: which issues, and what the person wrote."""
    fragments: list[str] = []
    for worklog in worklogs:
        text = f"{worklog.issue.key}: {worklog.comment}" if worklog.comment else worklog.issue.key
        if text not in fragments:
            fragments.append(text)
    comment = "; ".join(fragments)
    if len(comment) > COMMENT_MAX_LENGTH:
        comment = comment[: COMMENT_MAX_LENGTH - 1].rstrip() + "…"
    return comment


def build_timesheet(
    mapped: Iterable[MappedWorkLog], rounding: RoundingConfig
) -> list[TimesheetLine]:
    """Collapse worklogs to one line per (person, day, project, task)."""
    buckets: dict[tuple[str, date, str, str], list[MappedWorkLog]] = defaultdict(list)
    for item in mapped:
        key = (user_key(item.worklog), item.worklog.started_on, item.project_code, item.task_code)
        buckets[key].append(item)

    by_person_day: dict[tuple[str, date], list[tuple[tuple, list[MappedWorkLog]]]] = defaultdict(list)
    for key, items in buckets.items():
        by_person_day[(key[0], key[1])].append((key, items))

    lines: list[TimesheetLine] = []
    for (_user, _day), group in sorted(by_person_day.items(), key=lambda kv: (kv[0][0], kv[0][1])):
        group.sort(key=lambda entry: (entry[0][2], entry[0][3]))
        seconds = [sum(i.worklog.seconds for i in items) for _key, items in group]

        if rounding.increment_minutes > 0 and rounding.preserve_daily_total:
            increment_seconds = rounding.increment_minutes * 60
            units = allocate_units(seconds, increment_seconds)
            hours = [unit * rounding.increment_minutes / 60 for unit in units]
        elif rounding.increment_minutes > 0:
            increment_seconds = rounding.increment_minutes * 60
            hours = [
                _round_half_up(Decimal(value) / Decimal(increment_seconds))
                * rounding.increment_minutes
                / 60
                for value in seconds
            ]
        else:
            hours = [value / 3600 for value in seconds]

        for (key, items), hour_value in zip(group, hours):
            if hour_value <= 0:
                continue
            worklogs = [i.worklog for i in items]
            lines.append(
                TimesheetLine(
                    user=key[0],
                    day=key[1],
                    project_code=key[2],
                    task_code=key[3],
                    hours=round(hour_value, 4),
                    comment=_build_comment(worklogs),
                    worklog_ids=tuple(w.id for w in worklogs),
                )
            )
    return lines
