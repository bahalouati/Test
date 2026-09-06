"""Command line entry point."""

from __future__ import annotations

import argparse
import datetime as dt
import logging
import sys

from .config import Config, ConfigError
from .jira_client import JiraClient
from .ledger import Ledger
from .mapping import MappingError, MappingTable
from .models import SyncReport
from .sinks.base import get_sink
from .sync import run_sync


def _parse_date(value: str) -> dt.date:
    try:
        return dt.date.fromisoformat(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"expected YYYY-MM-DD, got {value!r}") from exc


def resolve_window(args: argparse.Namespace, today: dt.date | None = None) -> tuple[dt.date, dt.date]:
    """Work out the reporting window.

    ``--last-week`` means the Monday-to-Sunday week before the one containing
    today, which is what a Monday-morning scheduled run wants.
    """
    today = today or dt.date.today()
    if args.last_week:
        this_monday = today - dt.timedelta(days=today.weekday())
        start = this_monday - dt.timedelta(days=7)
        return start, start + dt.timedelta(days=6)
    if args.since and args.until:
        return args.since, args.until
    if args.since:
        return args.since, today
    # Default: the trailing week including today.
    return today - dt.timedelta(days=6), today


def _print_report(report: SyncReport, since: dt.date, until: dt.date) -> None:
    mode = "DRY RUN — nothing written" if report.dry_run else "wrote"
    print(f"\nWindow {since} .. {until}")
    print(f"  timesheet lines : {len(report.lines)} ({report.total_hours:g} h)")
    print(f"  unchanged       : {report.skipped_unchanged} worklog(s) already exported")

    if report.unmapped:
        print(f"  UNMAPPED        : {len(report.unmapped)} worklog(s) — add a mapping rule:")
        seen: set[tuple[str, str]] = set()
        for worklog in report.unmapped:
            key = (worklog.issue.project_key, worklog.issue.issue_type)
            if key not in seen:
                seen.add(key)
                print(f"      project={key[0]} type={key[1]} e.g. {worklog.issue.key}")

    if report.removed_worklog_ids:
        print(
            f"  REMOVED IN JIRA : {len(report.removed_worklog_ids)} worklog(s) were exported "
            "before but no longer exist — correct these in Sciforma by hand:"
        )
        print("      " + ", ".join(report.removed_worklog_ids[:20]))

    if report.dry_run:
        print(f"  {mode}")
        for line in list(report.lines)[:10]:
            print(
                f"      {line.user} {line.day} {line.project_code}/{line.task_code} "
                f"{line.hours:.2f}h"
            )
        if len(report.lines) > 10:
            print(f"      … {len(report.lines) - 10} more")
    else:
        for path in report.outputs:
            print(f"  {mode}: {path}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="jira-sciforma", description="Sync Jira worklogs into Sciforma timesheets"
    )
    parser.add_argument("-c", "--config", default="config/config.yaml", help="path to config YAML")
    parser.add_argument("-v", "--verbose", action="store_true", help="debug logging")
    sub = parser.add_subparsers(dest="command", required=True)

    sync = sub.add_parser("sync", help="fetch worklogs and produce timesheet output")
    sync.add_argument("--since", type=_parse_date, help="first day of the window (YYYY-MM-DD)")
    sync.add_argument("--until", type=_parse_date, help="last day of the window (YYYY-MM-DD)")
    sync.add_argument("--last-week", action="store_true", help="the previous Mon-Sun week")
    sync.add_argument("--dry-run", action="store_true", help="report only, write nothing")
    sync.add_argument(
        "--full", action="store_true", help="re-export everything, ignoring the ledger"
    )

    validate = sub.add_parser("validate", help="check config and mapping without calling Jira")
    validate.add_argument("--sample-project", help="report which rule would claim this project key")

    return parser


def _cmd_validate(config: Config, args: argparse.Namespace) -> int:
    mapping = MappingTable.load(config.mapping_file)
    print(f"config OK: {len(mapping.rules)} mapping rule(s), sink={config.sink.kind}")
    for rule in mapping.rules:
        criteria = []
        if rule.projects:
            criteria.append(f"project in {list(rule.projects)}")
        if rule.issue_types:
            criteria.append(f"type in {list(rule.issue_types)}")
        if rule.labels:
            criteria.append(f"label in {list(rule.labels)}")
        if rule.components:
            criteria.append(f"component in {list(rule.components)}")
        if rule.issue_key_pattern:
            criteria.append(f"key ~ {rule.issue_key_pattern}")
        print(
            f"  {rule.name}: {' and '.join(criteria) or 'catch-all'} "
            f"-> {rule.project_code}/{rule.task_code}"
        )
    if args.sample_project:
        from .models import Issue

        probe = Issue(
            key=f"{args.sample_project}-1",
            project_key=args.sample_project,
            summary="probe",
            issue_type="Task",
        )
        rule = mapping.resolve(probe)
        print(
            f"  {args.sample_project} -> "
            + (f"{rule.name} ({rule.project_code}/{rule.task_code})" if rule else "NO RULE MATCHES")
        )
    return 0


def _cmd_sync(config: Config, args: argparse.Namespace) -> int:
    since, until = resolve_window(args)
    mapping = MappingTable.load(config.mapping_file)
    client = JiraClient(config.jira)
    sink = get_sink(config.sink.kind, config.sink.output_dir, config.sink.options)

    with Ledger(config.ledger_path) as ledger:
        report = run_sync(
            config, client, mapping, sink, ledger, since, until,
            dry_run=args.dry_run, full_resync=args.full,
        )
    _print_report(report, since, until)
    return 1 if report.unmapped else 0


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(levelname)s %(name)s: %(message)s",
    )
    try:
        config = Config.load(args.config)
        if args.command == "validate":
            return _cmd_validate(config, args)
        return _cmd_sync(config, args)
    except (ConfigError, MappingError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    except FileNotFoundError as exc:
        print(f"error: file not found: {exc.filename}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
