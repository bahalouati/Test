import tempfile
import unittest
from pathlib import Path

from helpers import make_issue, make_worklog

from jira_sciforma.mapping import MappingError, MappingTable

MAPPING_YAML = """
rules:
  - name: support
    match:
      project: PLAT
      label: [support]
    project_code: SF-PLATFORM
    task_code: SUPPORT
  - name: platform
    match:
      project: PLAT
    project_code: SF-PLATFORM
    task_code: DELIVERY
  - name: bugs-anywhere
    match:
      issue_type: Bug
    project_code: SF-QA
    task_code: DEFECT
"""


def load(text):
    path = Path(tempfile.mkdtemp()) / "mapping.yaml"
    path.write_text(text, encoding="utf-8")
    return MappingTable.load(path)


class MappingTableTest(unittest.TestCase):
    def setUp(self):
        self.table = load(MAPPING_YAML)

    def test_first_matching_rule_wins(self):
        issue = make_issue(project="PLAT", labels=["support"])
        self.assertEqual(self.table.resolve(issue).name, "support")

    def test_falls_through_to_broader_rule(self):
        self.assertEqual(self.table.resolve(make_issue(project="PLAT")).name, "platform")

    def test_criteria_are_anded(self):
        # Right label, wrong project: the support rule must not claim it.
        issue = make_issue(project="MOB", issue_type="Bug", labels=["support"])
        self.assertEqual(self.table.resolve(issue).name, "bugs-anywhere")

    def test_unmatched_issue_resolves_to_none(self):
        self.assertIsNone(self.table.resolve(make_issue(project="OTHER")))

    def test_apply_splits_mapped_and_unmapped(self):
        worklogs = [
            make_worklog("1", make_issue(project="PLAT")),
            make_worklog("2", make_issue(project="OTHER", key="OTHER-9")),
        ]
        mapped, unmapped = self.table.apply(worklogs)
        self.assertEqual([m.worklog.id for m in mapped], ["1"])
        self.assertEqual([w.id for w in unmapped], ["2"])
        self.assertEqual(mapped[0].task_code, "DELIVERY")

    def test_issue_key_regex(self):
        table = load(
            "rules:\n"
            "  - name: legacy\n"
            "    match:\n"
            "      issue_key_matches: '^LEG-'\n"
            "    project_code: SF-LEGACY\n"
            "    task_code: MAINT\n"
        )
        self.assertEqual(table.resolve(make_issue(key="LEG-4", project="LEG")).name, "legacy")
        self.assertIsNone(table.resolve(make_issue(key="NEW-4", project="NEW")))

    def test_catch_all_rule_matches_anything(self):
        table = load("rules:\n  - name: all\n    project_code: X\n    task_code: Y\n")
        self.assertEqual(table.resolve(make_issue(project="ANYTHING")).name, "all")

    def test_missing_target_codes_is_an_error(self):
        with self.assertRaises(MappingError):
            load("rules:\n  - name: broken\n    match:\n      project: PLAT\n")

    def test_missing_rules_list_is_an_error(self):
        with self.assertRaises(MappingError):
            load("something_else: 1\n")


if __name__ == "__main__":
    unittest.main()
