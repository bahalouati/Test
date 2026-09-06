"""Sciforma REST sink — placeholder pending API access.

Deliberately unimplemented: the endpoint paths, auth scheme and timesheet
payload shape differ across Sciforma versions and installations, and guessing
them would produce code that looks finished and silently posts nothing. Once
you have API docs and a service account, fill in ``_authenticate`` and
``_post_timesheet_line``; nothing else in the pipeline needs to change.

What to confirm with your Sciforma administrator:
  * base URL and API version of the instance
  * auth scheme (service-account credentials, OAuth client, or API key)
  * timesheet endpoint, and whether entries post per line or per week
  * how a Jira user maps to a Sciforma resource id
  * whether submitted timesheets can be amended, or only draft ones
"""

from __future__ import annotations

from datetime import date
from typing import Sequence

from ..models import TimesheetLine
from .base import TimesheetSink


class SciformaRestSink(TimesheetSink):
    def __init__(self, base_url: str = "", verify_ssl: bool = True, **_options: object):
        self.base_url = base_url.rstrip("/")
        self.verify_ssl = verify_ssl

    def write(self, lines: Sequence[TimesheetLine], since: date, until: date) -> list[str]:
        raise NotImplementedError(
            "The Sciforma REST sink is a stub. Use sink.kind='csv' until API access is "
            "available, then implement _post_timesheet_line() in this module."
        )

    def _authenticate(self) -> str:
        raise NotImplementedError("Set up Sciforma auth here; return a token or session id.")

    def _post_timesheet_line(self, token: str, line: TimesheetLine) -> str:
        raise NotImplementedError("POST one timesheet line; return its Sciforma entry id.")
