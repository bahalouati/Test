"""Shared fixtures: builders for issues and worklogs, and a fake Jira source."""

from __future__ import annotations

import datetime as dt
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

from jira_sciforma.models import Issue, WorkLog  # noqa: E402


def make_issue(key="PLAT-1", project="PLAT", issue_type="Task", labels=(), components=()):
    return Issue(
        key=key,
        project_key=project,
        summary=f"summary for {key}",
        issue_type=issue_type,
        labels=tuple(labels),
        components=tuple(components),
    )


def make_worklog(
    worklog_id="1",
    issue=None,
    seconds=3600,
    day="2026-08-10",
    email="dev@example.com",
    comment="",
):
    return WorkLog(
        id=worklog_id,
        issue=issue or make_issue(),
        author_account_id="acc-1",
        author_display_name="Dev Person",
        author_email=email,
        started_on=dt.date.fromisoformat(day),
        seconds=seconds,
        comment=comment,
        updated="2026-08-10T12:00:00.000+0000",
    )


class FakeSource:
    def __init__(self, worklogs):
        self.worklogs = list(worklogs)

    def fetch_worklogs(self, since, until):
        return [w for w in self.worklogs if since <= w.started_on <= until]
