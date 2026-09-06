import datetime as dt
import tempfile
import unittest
from pathlib import Path

from helpers import make_worklog

from jira_sciforma.ledger import Ledger


class LedgerTest(unittest.TestCase):
    def setUp(self):
        self.path = Path(tempfile.mkdtemp()) / "ledger.sqlite"
        self.ledger = Ledger(self.path)
        self.addCleanup(self.ledger.close)

    def test_first_run_treats_everything_as_changed(self):
        changed, unchanged = self.ledger.select_changed([make_worklog("1"), make_worklog("2")])
        self.assertEqual(len(changed), 2)
        self.assertEqual(unchanged, 0)

    def test_recorded_worklogs_are_skipped_on_re_run(self):
        worklogs = [make_worklog("1"), make_worklog("2")]
        self.ledger.record(worklogs, sink_ref="out.csv")
        changed, unchanged = self.ledger.select_changed(worklogs)
        self.assertEqual(changed, [])
        self.assertEqual(unchanged, 2)

    def test_edited_worklog_is_re_exported(self):
        self.ledger.record([make_worklog("1", seconds=3600)], sink_ref="out.csv")
        changed, unchanged = self.ledger.select_changed([make_worklog("1", seconds=7200)])
        self.assertEqual([w.id for w in changed], ["1"])
        self.assertEqual(unchanged, 0)

    def test_recording_twice_updates_rather_than_duplicates(self):
        self.ledger.record([make_worklog("1", seconds=3600)], sink_ref="a.csv")
        self.ledger.record([make_worklog("1", seconds=7200)], sink_ref="b.csv")
        self.assertEqual(len(self.ledger.known_hashes()), 1)

    def test_find_removed_reports_worklogs_gone_from_jira(self):
        self.ledger.record(
            [make_worklog("1", day="2026-08-10"), make_worklog("2", day="2026-08-11")],
            sink_ref="out.csv",
        )
        removed = self.ledger.find_removed(
            dt.date(2026, 8, 10), dt.date(2026, 8, 12), present_ids=["1"]
        )
        self.assertEqual(removed, ["2"])

    def test_find_removed_ignores_other_windows(self):
        self.ledger.record([make_worklog("1", day="2026-07-01")], sink_ref="out.csv")
        removed = self.ledger.find_removed(
            dt.date(2026, 8, 10), dt.date(2026, 8, 12), present_ids=[]
        )
        self.assertEqual(removed, [])

    def test_forget_drops_entries(self):
        self.ledger.record([make_worklog("1")], sink_ref="out.csv")
        self.ledger.forget(["1"])
        self.assertEqual(self.ledger.known_hashes(), {})

    def test_survives_reopen(self):
        self.ledger.record([make_worklog("1")], sink_ref="out.csv")
        self.ledger.close()
        with Ledger(self.path) as reopened:
            self.assertEqual(len(reopened.known_hashes()), 1)


if __name__ == "__main__":
    unittest.main()
