# jira-sciforma-sync

Pulls Jira worklogs and turns them into Sciforma timesheet lines, so nobody
retypes their week by hand.

Runs as a scheduled batch job. Each run fetches the worklogs started in a date
window, maps each Jira issue to a Sciforma project/task, collapses everything to
one line per person / day / task, and hands the result to a **sink**.

## Status

The write side is behind one interface (`sinks/base.py`) with two implementations:

| Sink            | State  | Use when                                              |
| --------------- | ------ | ----------------------------------------------------- |
| `csv`           | Works  | No Sciforma API access — export files, people import them |
| `sciforma_rest` | Stub   | Once you have API docs and a service account          |

`sciforma_rest` deliberately raises `NotImplementedError` rather than guessing
endpoint shapes; see the module docstring for what to ask your Sciforma admin.
Filling it in is a config change (`sink.kind`) for everyone else — no pipeline
code moves.

## Quick start

```bash
pip install -e .

cp config/config.example.yaml config/config.yaml
cp config/mapping.example.yaml config/mapping.yaml
# edit both: your Jira URL, your JQL filter, your Sciforma project/task codes

export JIRA_EMAIL=you@company.com
export JIRA_API_TOKEN=...   # id.atlassian.com/manage-profile/security/api-tokens

jira-sciforma validate                      # check config + mapping, no network
jira-sciforma sync --last-week --dry-run    # see what it would produce
jira-sciforma sync --last-week              # write the CSVs to out/
```

## Commands

```
jira-sciforma validate [--sample-project KEY]   show rules, test one project key
jira-sciforma sync --last-week                  previous Mon-Sun week
jira-sciforma sync --since 2026-08-01 --until 2026-08-31
jira-sciforma sync ... --dry-run                report only, write nothing
jira-sciforma sync ... --full                   re-export, ignoring the ledger
```

`sync` exits non-zero when any worklog matched no mapping rule, so a scheduled
run surfaces mapping gaps instead of silently dropping time.

## Mapping

`config/mapping.yaml` holds an ordered rule list; the first match wins.

```yaml
rules:
  - name: platform-support
    match:
      project: PLAT          # Jira project key(s)
      label: [support]       # any of these labels
    project_code: SF-PLATFORM  # Sciforma
    task_code: SUPPORT
```

Criteria (`project`, `issue_type`, `label`, `component`, `issue_key_matches`)
are ANDed; a rule with no `match:` block is a catch-all. Drop the catch-all if
you would rather have unmapped time reported loudly than absorbed into an
"overhead" bucket.

## Rounding

Sciforma wants tidy hour figures; Jira gives arbitrary seconds. With
`preserve_daily_total: true` (the default) each person's day is rounded to the
configured increment and then split across their lines by largest-remainder
allocation, so **the day total is identical before and after rounding**. Entries
smaller than one increment keep a unit borrowed from the largest line rather
than being absorbed into it. Set `increment_minutes: 0` to disable rounding.

## Re-runs, edits and deletions

A SQLite ledger (`.state/ledger.sqlite`) fingerprints every exported worklog:

- **re-run** — already-exported worklogs are skipped, so the same day never
  double-books
- **edited in Jira** — the fingerprint changes and the worklog is re-exported
- **deleted in Jira** — reported as `REMOVED IN JIRA` with the ids, for manual
  correction (the CSV sink cannot retract a row someone already imported)

The ledger is state: the scheduled workflow caches it between runs. Lose it and
the next run re-exports the window.

## Scheduling

`.github/workflows/sync.yml` runs every Monday 06:00 UTC for the week that just
closed, publishes the run summary, and uploads `out/` as an artifact. Set
`JIRA_EMAIL` and `JIRA_API_TOKEN` as repository secrets and `JIRA_BASE_URL` as a
variable. Cron or a container scheduler works the same way — the CLI has no CI
dependency.

## Tests

```bash
python -m unittest discover -s tests -t tests -v
```

No network and no credentials required; the Jira layer is exercised through a
fake session.

## Layout

```
src/jira_sciforma/
  cli.py           argument parsing, date windows, run report
  config.py        YAML + environment, secrets never in the file
  jira_client.py   JQL search -> per-issue worklogs -> normalised WorkLog
  mapping.py       ordered rules, first match wins
  aggregate.py     grouping and largest-remainder rounding
  ledger.py        SQLite idempotency, edit and deletion detection
  sync.py          the pipeline
  sinks/           base interface, csv_sink, sciforma_rest (stub)
```

## Known limits

- **Sciforma writes are manual** until the REST sink is implemented.
- **Deletions need a human.** The tool reports them; it cannot un-import a row.
- **Rounding moves minutes between tasks** within a person-day. That is inherent
  to rounding; `increment_minutes: 0` avoids it.
- **Jira Cloud API v3.** Server/Data Center needs the paths in `jira_client.py`
  changed to `/rest/api/2`; Tempo would be a new source class behind the same
  `fetch_worklogs` shape.
- The worklog day is taken in the worklog's own timezone offset — the day the
  person saw when booking, not the UTC day.
