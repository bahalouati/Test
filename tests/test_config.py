import tempfile
import unittest
from pathlib import Path

import helpers  # noqa: F401  (puts src on the path)

from jira_sciforma.config import Config, ConfigError

FULL_ENV = {
    "JIRA_EMAIL": "bot@example.com",
    "JIRA_API_TOKEN": "secret-token",
}

BASE_YAML = """
jira:
  base_url: https://example.atlassian.net
  jql_filter: project = PLAT
mapping_file: config/mapping.yaml
rounding:
  increment_minutes: 6
  preserve_daily_total: false
sink:
  kind: csv
  output_dir: exports
  split_per_user: false
ledger_path: .state/x.sqlite
"""


def write(text):
    path = Path(tempfile.mkdtemp()) / "config.yaml"
    path.write_text(text, encoding="utf-8")
    return path


class ConfigLoadTest(unittest.TestCase):
    def test_loads_all_sections(self):
        config = Config.load(write(BASE_YAML), env=FULL_ENV)
        self.assertEqual(config.jira.base_url, "https://example.atlassian.net")
        self.assertEqual(config.jira.jql_filter, "project = PLAT")
        self.assertEqual(config.rounding.increment_minutes, 6)
        self.assertFalse(config.rounding.preserve_daily_total)
        self.assertEqual(config.sink.output_dir, "exports")
        self.assertEqual(config.ledger_path, ".state/x.sqlite")

    def test_credentials_come_from_the_environment(self):
        config = Config.load(write(BASE_YAML), env=FULL_ENV)
        self.assertEqual(config.jira.auth, ("bot@example.com", "secret-token"))

    def test_unknown_sink_keys_become_sink_options(self):
        # split_per_user is a CsvSink kwarg, not a Config field.
        config = Config.load(write(BASE_YAML), env=FULL_ENV)
        self.assertEqual(config.sink.options, {"split_per_user": False})

    def test_base_url_can_come_from_env(self):
        yaml_text = "mapping_file: config/mapping.yaml\n"
        env = {**FULL_ENV, "JIRA_BASE_URL": "https://env.atlassian.net/"}
        config = Config.load(write(yaml_text), env=env)
        self.assertEqual(config.jira.base_url, "https://env.atlassian.net")

    def test_missing_token_is_reported(self):
        with self.assertRaises(ConfigError) as ctx:
            Config.load(write(BASE_YAML), env={"JIRA_EMAIL": "a@b.c"})
        self.assertIn("JIRA_API_TOKEN", str(ctx.exception))

    def test_missing_mapping_file_is_reported(self):
        with self.assertRaises(ConfigError) as ctx:
            Config.load(write("jira:\n  base_url: https://x\n"), env=FULL_ENV)
        self.assertIn("mapping_file", str(ctx.exception))

    def test_defaults_apply_when_sections_are_absent(self):
        config = Config.load(write("mapping_file: m.yaml\n"), env={**FULL_ENV, "JIRA_BASE_URL": "https://x"})
        self.assertEqual(config.sink.kind, "csv")
        self.assertEqual(config.rounding.increment_minutes, 15)
        self.assertTrue(config.rounding.preserve_daily_total)


if __name__ == "__main__":
    unittest.main()
