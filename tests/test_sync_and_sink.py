"""End-to-end pipeline tests plus CSV output and CLI window maths."""

import argparse
import csv
import datetime as dt
import tempfile
import unittest
from pathlib import Path

from helpers import FakeSource, make_issue, make_worklog

from jira_sciforma.cli import resolve_window
from jira_sciforma.config import Config, JiraConfig, RoundingConfig, SinkConfig
from jira_sciforma.ledger import Ledger
from jira_sciforma.mapping import MappingTable
from jira_sciforma.models import TimesheetLine
from jira_sciforma.sinks.base import get_sink
from jira_sciforma.sinks.csv_sink import CsvSink
from jira_sciforma.sinks.sciforma_rest import SciformaRestSink
from jira_sciforma.sync import run_sync

MAPPING = """
rules:
  - name: platform
    match:
      project: PLAT
    project_code: SF-PLATFORM
    task_code: DELIVERY
"""

SINCE = dt.date(2026, 8, 10)
UNTIL = dt.date(2026, 8, 16)


class CsvSinkTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.lines = [
            TimesheetLine("a@example.com", dt.date(2026, 8, 10), "SF-A", "BUILD", 7.5, "PLAT-1"),
            TimesheetLine("b@example.com", dt.date(2026, 8, 11), "SF-B", "SUPPORT", 2.0, "MOB-2"),
        ]

    def test_writes_combined_and_per_user_files(self):
        outputs = CsvSink(output_dir=str(self.dir)).write(self.lines, SINCE, UNTIL)
        self.assertEqual(len(outputs), 3)
        for path in outputs:
            self.assertTrue(Path(path).exists())

    def test_combined_file_contents(self):
        outputs = CsvSink(output_dir=str(self.dir), split_per_user=False).write(
            self.lines, SINCE, UNTIL
        )
        with Path(outputs[0]).open(encoding="utf-8") as handle:
            rows = list(csv.DictReader(handle))
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0]["Resource"], "a@example.com")
        self.assertEqual(rows[0]["Date"], "2026-08-10")
        self.assertEqual(rows[0]["Hours"], "7.50")
        self.assertEqual(rows[0]["TaskCode"], "BUILD")

    def test_column_names_are_configurable(self):
        sink = CsvSink(
            output_dir=str(self.dir),
            split_per_user=False,
            columns={"resource": "Employee", "hours": "Effort"},
        )
        outputs = sink.write(self.lines, SINCE, UNTIL)
        header = Path(outputs[0]).read_text(encoding="utf-8").splitlines()[0]
        self.assertIn("Employee", header)
        self.assertIn("Effort", header)

    def test_no_lines_writes_nothing(self):
        self.assertEqual(CsvSink(output_dir=str(self.dir)).write([], SINCE, UNTIL), [])

    def test_user_filenames_are_filesystem_safe(self):
        lines = [TimesheetLine("a b/c@x.com", dt.date(2026, 8, 10), "P", "T", 1.0, "")]
        outputs = CsvSink(output_dir=str(self.dir)).write(lines, SINCE, UNTIL)
        self.assertTrue(any("a_b_c" in path for path in outputs))


class SinkFactoryTest(unittest.TestCase):
    def test_unknown_kind_is_rejected(self):
        with self.assertRaises(ValueError):
            get_sink("carrier-pigeon", "out", {})

    def test_rest_sink_fails_loudly_rather_than_silently(self):
        with self.assertRaises(NotImplementedError):
            SciformaRestSink().write([], SINCE, UNTIL)


class SyncTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        mapping_path = self.dir / "mapping.yaml"
        mapping_path.write_text(MAPPING, encoding="utf-8")
        self.mapping = MappingTable.load(mapping_path)
        self.config = Config(
            jira=JiraConfig(base_url="https://x", email="a@b.c", api_token="t"),
            mapping_file=str(mapping_path),
            sink=SinkConfig(kind="csv", output_dir=str(self.dir / "out")),
            rounding=RoundingConfig(increment_minutes=15),
            ledger_path=str(self.dir / "ledger.sqlite"),
        )
        self.sink = CsvSink(output_dir=str(self.dir / "out"))

    def _run(self, worklogs, **kwargs):
        with Ledger(self.config.ledger_path) as ledger:
            return run_sync(
                self.config, FakeSource(worklogs), self.mapping, self.sink, ledger,
                SINCE, UNTIL, **kwargs,
            )

    def test_happy_path_produces_lines_and_files(self):
        report = self._run([make_worklog("1", seconds=3600, day="2026-08-10")])
        self.assertEqual(len(report.lines), 1)
        self.assertEqual(report.total_hours, 1.0)
        self.assertTrue(report.outputs)

    def test_second_run_is_a_no_op(self):
        worklogs = [make_worklog("1", seconds=3600, day="2026-08-10")]
        self._run(worklogs)
        second = self._run(worklogs)
        self.assertEqual(second.lines, [])
        self.assertEqual(second.skipped_unchanged, 1)
        self.assertEqual(second.outputs, [])

    def test_edited_worklog_is_picked_up_again(self):
        self._run([make_worklog("1", seconds=3600, day="2026-08-10")])
        second = self._run([make_worklog("1", seconds=7200, day="2026-08-10")])
        self.assertEqual(len(second.lines), 1)
        self.assertEqual(second.lines[0].hours, 2.0)

    def test_full_resync_ignores_the_ledger(self):
        worklogs = [make_worklog("1", seconds=3600, day="2026-08-10")]
        self._run(worklogs)
        second = self._run(worklogs, full_resync=True)
        self.assertEqual(len(second.lines), 1)

    def test_dry_run_writes_nothing_and_leaves_ledger_clean(self):
        worklogs = [make_worklog("1", seconds=3600, day="2026-08-10")]
        report = self._run(worklogs, dry_run=True)
        self.assertTrue(report.lines)
        self.assertEqual(report.outputs, [])
        self.assertFalse((self.dir / "out").exists())
        # A real run afterwards must still see the worklog as new.
        self.assertEqual(len(self._run(worklogs).lines), 1)

    def test_unmapped_worklogs_are_reported_not_exported(self):
        worklogs = [
            make_worklog("1", make_issue(project="PLAT"), seconds=3600, day="2026-08-10"),
            make_worklog("2", make_issue(key="ZZZ-1", project="ZZZ"), seconds=3600, day="2026-08-10"),
        ]
        report = self._run(worklogs)
        self.assertEqual(len(report.lines), 1)
        self.assertEqual([w.id for w in report.unmapped], ["2"])

    def test_worklog_deleted_in_jira_is_flagged(self):
        self._run([make_worklog("1", seconds=3600, day="2026-08-10")])
        report = self._run([])
        self.assertEqual(list(report.removed_worklog_ids), ["1"])


class ResolveWindowTest(unittest.TestCase):
    @staticmethod
    def args(**kwargs):
        return argparse.Namespace(
            since=kwargs.get("since"), until=kwargs.get("until"),
            last_week=kwargs.get("last_week", False),
        )

    def test_last_week_is_the_previous_monday_to_sunday(self):
        # 2026-08-13 is a Thursday; the previous full week is Mon 3rd - Sun 9th.
        since, until = resolve_window(self.args(last_week=True), today=dt.date(2026, 8, 13))
        self.assertEqual((since, until), (dt.date(2026, 8, 3), dt.date(2026, 8, 9)))

    def test_last_week_from_a_monday(self):
        since, until = resolve_window(self.args(last_week=True), today=dt.date(2026, 8, 10))
        self.assertEqual((since, until), (dt.date(2026, 8, 3), dt.date(2026, 8, 9)))

    def test_explicit_range(self):
        window = resolve_window(
            self.args(since=dt.date(2026, 1, 1), until=dt.date(2026, 1, 31)),
            today=dt.date(2026, 8, 13),
        )
        self.assertEqual(window, (dt.date(2026, 1, 1), dt.date(2026, 1, 31)))

    def test_since_alone_runs_to_today(self):
        window = resolve_window(self.args(since=dt.date(2026, 8, 1)), today=dt.date(2026, 8, 13))
        self.assertEqual(window, (dt.date(2026, 8, 1), dt.date(2026, 8, 13)))

    def test_default_is_the_trailing_week(self):
        window = resolve_window(self.args(), today=dt.date(2026, 8, 13))
        self.assertEqual(window, (dt.date(2026, 8, 7), dt.date(2026, 8, 13)))


if __name__ == "__main__":
    unittest.main()
