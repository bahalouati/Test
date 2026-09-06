"""Configuration loading.

Secrets come from the environment, everything else from a YAML file, so the
config can live in git while credentials stay in the CI secret store.
"""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml


class ConfigError(Exception):
    """Raised when configuration is missing or self-contradictory."""


@dataclass
class JiraConfig:
    base_url: str
    email: str
    api_token: str
    jql_filter: str = ""
    page_size: int = 50
    timeout_seconds: int = 30

    @property
    def auth(self) -> tuple[str, str]:
        return (self.email, self.api_token)


@dataclass
class RoundingConfig:
    """How raw seconds become the hour figures Sciforma accepts.

    ``increment_minutes`` of 0 means "don't round". Otherwise each line is
    rounded to that increment using largest-remainder allocation, which keeps
    the per-person per-day total identical to the unrounded total.
    """

    increment_minutes: int = 15
    preserve_daily_total: bool = True


@dataclass
class SinkConfig:
    kind: str = "csv"
    output_dir: str = "out"
    options: dict[str, Any] = field(default_factory=dict)


@dataclass
class Config:
    jira: JiraConfig
    mapping_file: str
    sink: SinkConfig = field(default_factory=SinkConfig)
    rounding: RoundingConfig = field(default_factory=RoundingConfig)
    ledger_path: str = ".state/ledger.sqlite"

    @classmethod
    def load(cls, path: str | Path, env: dict[str, str] | None = None) -> "Config":
        env = os.environ if env is None else env
        raw = yaml.safe_load(Path(path).read_text(encoding="utf-8")) or {}

        jira_raw = raw.get("jira") or {}
        base_url = jira_raw.get("base_url") or env.get("JIRA_BASE_URL", "")
        email = env.get("JIRA_EMAIL", "")
        token = env.get("JIRA_API_TOKEN", "")
        missing = [
            name
            for name, value in (
                ("jira.base_url (or JIRA_BASE_URL)", base_url),
                ("JIRA_EMAIL", email),
                ("JIRA_API_TOKEN", token),
            )
            if not value
        ]
        if missing:
            raise ConfigError("missing required configuration: " + ", ".join(missing))

        mapping_file = raw.get("mapping_file")
        if not mapping_file:
            raise ConfigError("mapping_file is required")

        sink_raw = dict(raw.get("sink") or {})
        rounding_raw = dict(raw.get("rounding") or {})

        return cls(
            jira=JiraConfig(
                base_url=base_url.rstrip("/"),
                email=email,
                api_token=token,
                jql_filter=jira_raw.get("jql_filter", ""),
                page_size=int(jira_raw.get("page_size", 50)),
                timeout_seconds=int(jira_raw.get("timeout_seconds", 30)),
            ),
            mapping_file=mapping_file,
            sink=SinkConfig(
                kind=sink_raw.pop("kind", "csv"),
                output_dir=sink_raw.pop("output_dir", "out"),
                options=sink_raw,
            ),
            rounding=RoundingConfig(
                increment_minutes=int(rounding_raw.get("increment_minutes", 15)),
                preserve_daily_total=bool(rounding_raw.get("preserve_daily_total", True)),
            ),
            ledger_path=raw.get("ledger_path", ".state/ledger.sqlite"),
        )
