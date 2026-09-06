import datetime as dt
import unittest

import helpers  # noqa: F401  (puts src on the path)

from jira_sciforma.config import JiraConfig
from jira_sciforma.jira_client import JiraClient, _parse_started, _plain_text


class ParseStartedTest(unittest.TestCase):
    def test_jira_offset_without_colon(self):
        self.assertEqual(_parse_started("2026-08-14T09:30:00.000+0200"), dt.date(2026, 8, 14))

    def test_iso_offset_with_colon(self):
        self.assertEqual(_parse_started("2026-08-14T09:30:00.000+02:00"), dt.date(2026, 8, 14))

    def test_date_uses_the_worklog_offset_not_utc(self):
        # 00:30 local on the 15th in +02:00 is still the 14th in UTC; the person
        # booked it on the 15th, so that is the day that must land in Sciforma.
        self.assertEqual(_parse_started("2026-08-15T00:30:00.000+0200"), dt.date(2026, 8, 15))


class PlainTextTest(unittest.TestCase):
    def test_none_and_empty(self):
        self.assertEqual(_plain_text(None), "")
        self.assertEqual(_plain_text(""), "")

    def test_server_style_plain_string(self):
        self.assertEqual(_plain_text("  did the thing  "), "did the thing")

    def test_atlassian_document_format_is_flattened(self):
        adf = {
            "type": "doc",
            "content": [
                {"type": "paragraph", "content": [{"type": "text", "text": "fixed login"}]},
                {"type": "paragraph", "content": [{"type": "text", "text": "and logout"}]},
            ],
        }
        self.assertEqual(_plain_text(adf), "fixed login and logout")


class FakeResponse:
    def __init__(self, payload, status_code=200):
        self._payload = payload
        self.status_code = status_code
        self.text = str(payload)

    def json(self):
        return self._payload


class FakeSession:
    """Stands in for requests.Session, replaying canned pages by URL path."""

    def __init__(self, routes):
        self.routes = routes
        self.calls = []
        self.auth = None
        self.headers = {}

    def get(self, url, params=None, timeout=None):
        self.calls.append((url, params))
        for fragment, pages in self.routes.items():
            if fragment in url:
                return FakeResponse(pages.pop(0) if len(pages) > 1 else pages[0])
        raise AssertionError(f"unexpected URL {url}")


ISSUE = {
    "key": "PLAT-1",
    "fields": {
        "project": {"key": "PLAT"},
        "summary": "Login is broken",
        "issuetype": {"name": "Bug"},
        "labels": ["support"],
        "components": [{"name": "auth"}],
        "parent": {"key": "PLAT-100"},
    },
}


class JiraClientTest(unittest.TestCase):
    def _client(self, routes):
        config = JiraConfig(base_url="https://example.atlassian.net", email="a@b.c", api_token="t")
        return JiraClient(config, session=FakeSession(routes))

    def test_fetch_maps_issue_and_worklog_fields(self):
        client = self._client(
            {
                "/search": [{"issues": [ISSUE], "total": 1}],
                "/worklog": [
                    {
                        "worklogs": [
                            {
                                "id": "9001",
                                "author": {
                                    "accountId": "acc-1",
                                    "displayName": "Dev",
                                    "emailAddress": "dev@example.com",
                                },
                                "started": "2026-08-10T09:00:00.000+0200",
                                "timeSpentSeconds": 5400,
                                "comment": "fixed it",
                                "updated": "2026-08-10T10:00:00.000+0200",
                            }
                        ],
                        "total": 1,
                    }
                ],
            }
        )
        worklogs = client.fetch_worklogs(dt.date(2026, 8, 10), dt.date(2026, 8, 16))
        self.assertEqual(len(worklogs), 1)
        entry = worklogs[0]
        self.assertEqual(entry.id, "9001")
        self.assertEqual(entry.seconds, 5400)
        self.assertEqual(entry.author_email, "dev@example.com")
        self.assertEqual(entry.issue.project_key, "PLAT")
        self.assertEqual(entry.issue.components, ("auth",))
        self.assertEqual(entry.issue.parent_key, "PLAT-100")

    def test_worklogs_outside_the_window_are_dropped(self):
        client = self._client(
            {
                "/search": [{"issues": [ISSUE], "total": 1}],
                "/worklog": [
                    {
                        "worklogs": [
                            {
                                "id": "1",
                                "author": {"accountId": "a"},
                                "started": "2026-09-30T09:00:00.000+0000",
                                "timeSpentSeconds": 3600,
                            }
                        ],
                        "total": 1,
                    }
                ],
            }
        )
        self.assertEqual(client.fetch_worklogs(dt.date(2026, 8, 10), dt.date(2026, 8, 16)), [])

    def test_zero_second_worklogs_are_dropped(self):
        client = self._client(
            {
                "/search": [{"issues": [ISSUE], "total": 1}],
                "/worklog": [
                    {
                        "worklogs": [
                            {
                                "id": "1",
                                "author": {"accountId": "a"},
                                "started": "2026-08-10T09:00:00.000+0000",
                                "timeSpentSeconds": 0,
                            }
                        ],
                        "total": 1,
                    }
                ],
            }
        )
        self.assertEqual(client.fetch_worklogs(dt.date(2026, 8, 10), dt.date(2026, 8, 16)), [])

    def test_jql_includes_window_and_configured_filter(self):
        config = JiraConfig(
            base_url="https://x", email="a@b.c", api_token="t", jql_filter="project = PLAT"
        )
        client = JiraClient(config, session=FakeSession({}))
        jql = client._build_jql(dt.date(2026, 8, 10), dt.date(2026, 8, 16))
        self.assertIn('worklogDate >= "2026-08-10"', jql)
        self.assertIn('worklogDate <= "2026-08-16"', jql)
        self.assertIn("(project = PLAT)", jql)

    def test_inverted_window_is_rejected(self):
        client = self._client({})
        with self.assertRaises(ValueError):
            client.fetch_worklogs(dt.date(2026, 8, 16), dt.date(2026, 8, 10))


if __name__ == "__main__":
    unittest.main()
