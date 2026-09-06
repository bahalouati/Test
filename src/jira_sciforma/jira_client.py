"""Jira Cloud worklog source.

Strategy: one JQL search for issues that have worklogs in the window, then the
worklog endpoint per issue. That costs more requests than the ``worklog/updated``
firehose, but it keeps the project filter server-side and hands back the issue
fields the mapping rules need in the same pass.
"""

from __future__ import annotations

import datetime as dt
import logging
from typing import Any, Iterator

import requests

from .config import JiraConfig
from .models import Issue, WorkLog

log = logging.getLogger(__name__)

ISSUE_FIELDS = "project,summary,issuetype,labels,components,parent"


class JiraError(Exception):
    """A Jira request failed in a way retrying will not fix."""


def _parse_started(value: str) -> dt.date:
    """Jira sends ``2026-08-14T09:30:00.000+0200``; we only care about the day.

    The date is taken in the worklog's own offset, which is what the person
    booking the time saw on their screen.
    """
    cleaned = value.strip()
    if len(cleaned) >= 5 and (cleaned[-5] in "+-") and cleaned[-3] != ":":
        cleaned = cleaned[:-2] + ":" + cleaned[-2:]
    return dt.datetime.fromisoformat(cleaned).date()


def _plain_text(comment: Any) -> str:
    """Flatten a comment to text.

    Jira Cloud returns Atlassian Document Format; Server returns a plain string.
    """
    if not comment:
        return ""
    if isinstance(comment, str):
        return comment.strip()
    out: list[str] = []

    def walk(node: Any) -> None:
        if isinstance(node, dict):
            if node.get("type") == "text" and isinstance(node.get("text"), str):
                out.append(node["text"])
            for child in node.get("content") or []:
                walk(child)
        elif isinstance(node, list):
            for child in node:
                walk(child)

    walk(comment)
    return " ".join(" ".join(out).split())


class JiraClient:
    def __init__(self, config: JiraConfig, session: requests.Session | None = None):
        self.config = config
        self.session = session or requests.Session()
        self.session.auth = config.auth
        self.session.headers.update({"Accept": "application/json"})

    def _get(self, path: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
        url = f"{self.config.base_url}{path}"
        response = self.session.get(url, params=params, timeout=self.config.timeout_seconds)
        if response.status_code >= 400:
            raise JiraError(f"GET {path} -> {response.status_code}: {response.text[:300]}")
        return response.json()

    def _build_jql(self, since: dt.date, until: dt.date) -> str:
        clauses = [
            f'worklogDate >= "{since.isoformat()}"',
            f'worklogDate <= "{until.isoformat()}"',
        ]
        if self.config.jql_filter:
            clauses.append(f"({self.config.jql_filter})")
        return " AND ".join(clauses) + " ORDER BY key ASC"

    def _search_issues(self, since: dt.date, until: dt.date) -> Iterator[Issue]:
        start_at = 0
        jql = self._build_jql(since, until)
        while True:
            page = self._get(
                "/rest/api/3/search",
                {
                    "jql": jql,
                    "startAt": start_at,
                    "maxResults": self.config.page_size,
                    "fields": ISSUE_FIELDS,
                },
            )
            issues = page.get("issues") or []
            for raw in issues:
                yield self._to_issue(raw)
            start_at += len(issues)
            if not issues or start_at >= int(page.get("total", 0)):
                return

    @staticmethod
    def _to_issue(raw: dict[str, Any]) -> Issue:
        fields = raw.get("fields") or {}
        parent = fields.get("parent") or {}
        return Issue(
            key=raw["key"],
            project_key=(fields.get("project") or {}).get("key", ""),
            summary=fields.get("summary") or "",
            issue_type=(fields.get("issuetype") or {}).get("name", ""),
            labels=tuple(fields.get("labels") or ()),
            components=tuple(c.get("name", "") for c in fields.get("components") or ()),
            parent_key=parent.get("key"),
        )

    def _issue_worklogs(self, issue: Issue, since: dt.date, until: dt.date) -> Iterator[WorkLog]:
        # startedAfter is an inclusive epoch-ms floor; the upper bound is filtered
        # locally because Jira has no matching startedBefore on this endpoint.
        started_after = int(
            dt.datetime.combine(since, dt.time.min, tzinfo=dt.timezone.utc).timestamp() * 1000
        )
        start_at = 0
        while True:
            page = self._get(
                f"/rest/api/3/issue/{issue.key}/worklog",
                {
                    "startAt": start_at,
                    "maxResults": self.config.page_size,
                    "startedAfter": started_after,
                },
            )
            entries = page.get("worklogs") or []
            for raw in entries:
                worklog = self._to_worklog(raw, issue)
                if since <= worklog.started_on <= until:
                    yield worklog
            start_at += len(entries)
            if not entries or start_at >= int(page.get("total", 0)):
                return

    @staticmethod
    def _to_worklog(raw: dict[str, Any], issue: Issue) -> WorkLog:
        author = raw.get("author") or {}
        return WorkLog(
            id=str(raw["id"]),
            issue=issue,
            author_account_id=author.get("accountId") or author.get("name") or "unknown",
            author_display_name=author.get("displayName") or "unknown",
            author_email=author.get("emailAddress"),
            started_on=_parse_started(raw["started"]),
            seconds=int(raw.get("timeSpentSeconds") or 0),
            comment=_plain_text(raw.get("comment")),
            updated=raw.get("updated") or "",
        )

    def fetch_worklogs(self, since: dt.date, until: dt.date) -> list[WorkLog]:
        """Every worklog started within ``[since, until]`` that the JQL filter admits."""
        if since > until:
            raise ValueError("since must not be after until")
        worklogs: list[WorkLog] = []
        for issue in self._search_issues(since, until):
            for worklog in self._issue_worklogs(issue, since, until):
                if worklog.seconds > 0:
                    worklogs.append(worklog)
        log.info("fetched %d worklogs between %s and %s", len(worklogs), since, until)
        return worklogs
