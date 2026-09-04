"""Jira issue -> Sciforma project/task resolution.

Rules are evaluated top to bottom and the first match wins, so put specific
rules above broad ones. A rule with no criteria matches everything and acts as
the catch-all.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

import yaml

from .models import Issue, MappedWorkLog, WorkLog


class MappingError(Exception):
    """The mapping file is malformed."""


@dataclass(frozen=True)
class Rule:
    name: str
    project_code: str
    task_code: str
    projects: tuple[str, ...] = ()
    issue_types: tuple[str, ...] = ()
    labels: tuple[str, ...] = ()
    components: tuple[str, ...] = ()
    issue_key_pattern: str | None = None

    def matches(self, issue: Issue) -> bool:
        if self.projects and issue.project_key not in self.projects:
            return False
        if self.issue_types and issue.issue_type not in self.issue_types:
            return False
        if self.labels and not set(self.labels) & set(issue.labels):
            return False
        if self.components and not set(self.components) & set(issue.components):
            return False
        if self.issue_key_pattern and not re.search(self.issue_key_pattern, issue.key):
            return False
        return True


def _as_tuple(value: Any) -> tuple[str, ...]:
    if value is None:
        return ()
    if isinstance(value, str):
        return (value,)
    return tuple(str(item) for item in value)


class MappingTable:
    def __init__(self, rules: Iterable[Rule]):
        self.rules = list(rules)
        if not self.rules:
            raise MappingError("mapping file defines no rules")

    @classmethod
    def load(cls, path: str | Path) -> "MappingTable":
        raw = yaml.safe_load(Path(path).read_text(encoding="utf-8")) or {}
        entries = raw.get("rules")
        if not isinstance(entries, list):
            raise MappingError("mapping file must contain a top-level 'rules' list")

        rules = []
        for index, entry in enumerate(entries):
            if not isinstance(entry, dict):
                raise MappingError(f"rule #{index + 1} is not a mapping")
            missing = [k for k in ("project_code", "task_code") if not entry.get(k)]
            if missing:
                name = entry.get("name", f"#{index + 1}")
                raise MappingError(f"rule {name} is missing: {', '.join(missing)}")
            match = entry.get("match") or {}
            rules.append(
                Rule(
                    name=entry.get("name") or f"rule-{index + 1}",
                    project_code=str(entry["project_code"]),
                    task_code=str(entry["task_code"]),
                    projects=_as_tuple(match.get("project")),
                    issue_types=_as_tuple(match.get("issue_type")),
                    labels=_as_tuple(match.get("label")),
                    components=_as_tuple(match.get("component")),
                    issue_key_pattern=match.get("issue_key_matches"),
                )
            )
        return cls(rules)

    def resolve(self, issue: Issue) -> Rule | None:
        for rule in self.rules:
            if rule.matches(issue):
                return rule
        return None

    def apply(self, worklogs: Iterable[WorkLog]) -> tuple[list[MappedWorkLog], list[WorkLog]]:
        """Split worklogs into those a rule claimed and those nothing matched."""
        mapped: list[MappedWorkLog] = []
        unmapped: list[WorkLog] = []
        for worklog in worklogs:
            rule = self.resolve(worklog.issue)
            if rule is None:
                unmapped.append(worklog)
            else:
                mapped.append(
                    MappedWorkLog(
                        worklog=worklog,
                        project_code=rule.project_code,
                        task_code=rule.task_code,
                        rule_name=rule.name,
                    )
                )
        return mapped, unmapped
