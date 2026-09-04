import datetime as dt
import unittest

from helpers import make_issue, make_worklog

from jira_sciforma.aggregate import allocate_units, build_timesheet
from jira_sciforma.config import RoundingConfig
from jira_sciforma.models import MappedWorkLog


def mapped(worklog, project="SF-A", task="BUILD"):
    return MappedWorkLog(worklog=worklog, project_code=project, task_code=task, rule_name="r")


class AllocateUnitsTest(unittest.TestCase):
    def test_exact_multiples_are_unchanged(self):
        self.assertEqual(allocate_units([3600, 1800], 900), [4, 2])

    def test_day_total_is_preserved_across_rounding(self):
        seconds = [1000, 2000, 3000, 4000]  # 2h47m -> 11 quarter-hours
        units = allocate_units(seconds, 900)
        self.assertEqual(sum(units), 11)

    def test_sub_increment_entry_keeps_a_unit(self):
        # 8h on one task plus 5 minutes on another: the small entry must survive
        # rather than having its time absorbed by the large one.
        units = allocate_units([8 * 3600, 300], 900)
        self.assertEqual(units, [31, 1])
        self.assertEqual(sum(units), 32)

    def test_empty_input(self):
        self.assertEqual(allocate_units([], 900), [])

    def test_rejects_non_positive_increment(self):
        with self.assertRaises(ValueError):
            allocate_units([100], 0)


class BuildTimesheetTest(unittest.TestCase):
    def test_worklogs_on_same_day_and_task_collapse_to_one_line(self):
        issue = make_issue()
        entries = [
            mapped(make_worklog("1", issue, seconds=3600)),
            mapped(make_worklog("2", issue, seconds=1800)),
        ]
        lines = build_timesheet(entries, RoundingConfig())
        self.assertEqual(len(lines), 1)
        self.assertEqual(lines[0].hours, 1.5)
        self.assertEqual(lines[0].worklog_ids, ("1", "2"))

    def test_different_tasks_stay_separate(self):
        entries = [
            mapped(make_worklog("1", seconds=3600), task="BUILD"),
            mapped(make_worklog("2", seconds=3600), task="SUPPORT"),
        ]
        lines = build_timesheet(entries, RoundingConfig())
        self.assertEqual({l.task_code for l in lines}, {"BUILD", "SUPPORT"})

    def test_different_people_stay_separate(self):
        entries = [
            mapped(make_worklog("1", email="a@example.com")),
            mapped(make_worklog("2", email="b@example.com")),
        ]
        lines = build_timesheet(entries, RoundingConfig())
        self.assertEqual(len(lines), 2)

    def test_rounding_disabled_keeps_raw_hours(self):
        entries = [mapped(make_worklog("1", seconds=1000))]
        lines = build_timesheet(entries, RoundingConfig(increment_minutes=0))
        self.assertAlmostEqual(lines[0].hours, 1000 / 3600, places=4)

    def test_independent_rounding_mode(self):
        entries = [mapped(make_worklog("1", seconds=1000))]  # 16m40s -> 15m
        config = RoundingConfig(increment_minutes=15, preserve_daily_total=False)
        self.assertEqual(build_timesheet(entries, config)[0].hours, 0.25)

    def test_comment_carries_issue_provenance(self):
        entries = [mapped(make_worklog("1", comment="fixed login"))]
        self.assertEqual(build_timesheet(entries, RoundingConfig())[0].comment, "PLAT-1: fixed login")

    def test_line_date_matches_worklog_day(self):
        entries = [mapped(make_worklog("1", day="2026-08-12"))]
        self.assertEqual(build_timesheet(entries, RoundingConfig())[0].day, dt.date(2026, 8, 12))


if __name__ == "__main__":
    unittest.main()
