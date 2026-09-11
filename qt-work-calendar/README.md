# Work Calendar

A desktop work log: record what you did, see the month at a glance against an
eight-hour day, and hand out the spreadsheet when somebody asks for one.

Qt 6 Widgets, C++17, one SQLite file. No server, no account, no network unless
you ask it to talk to Jira.

```
Monday          Tuesday         Wednesday       Thursday        Friday
 7        8h     8       8h30    9        2h    10        5h    11    7h45
 PLAT-45  8h     PLAT-45 8h30    SUP-11   2h    PLAT-46   5h    PLAT-46 7h45
 ^ green         ^ green         ^ red          ^ red           ^ amber
```

## Why it exists

A monthly spreadsheet of Jira worklogs answers "did I book my eight hours?"
only after it has been generated, and only for the fields the script happened
to pull. This does the same job continuously, keeps far more than the script
did, and still produces the workbook at the end.

## What it does

**Calendar** - a month of cells, each coloured against that day's target:
green when the target is met, amber when it is close, red when it is well
short, grey for days nothing is expected of, blue for an absence. Each cell
lists the day's entries and its running total, with a small progress bar. A bar
chart under it plots the month against the target line.

**A day panel** - the selected day's entries, its balance, and every field of
the selected entry: issue, summary, project, type, priority, epic, sprint,
component, fix version, branch, tags, location, billable, notes, links, where
the entry came from and when it was last touched. Links are clickable.

**Entries** - every entry in a sortable table with **31 available columns**,
which ones and in which order being up to you (*Columns...*). Filter by text
across every field - shown or not - by date range, activity, project, sprint,
or billability.

**Reports** - logged against expected for any period, the balance, billable
share, days at target, the current and longest streak, and a breakdown by
project, issue, activity, sprint, fix version, epic, component, type, status,
location, tag or weekday. Plus a plain-text recap for the clipboard.

**A timer** - start it when you begin, stop it when you are done, and the entry
dialog opens prefilled. It survives closing the application, so a crash or a
Friday evening does not cost you the afternoon.

**Day types** - holiday, vacation, sick leave, half day, training, on call, or
a one-off custom target. A public holiday is no longer a red day with missing
hours, and it stops dragging the monthly balance down.

**Export** - a three-sheet `.xlsx` (worklogs with filters and live hyperlinks,
the colour-coded calendar, and a summary with the breakdowns), or CSV of
exactly what the filters are showing.

**Import** - CSV, or your own Jira worklogs for a period. Re-importing the same
period refreshes what it imported before instead of duplicating it, and never
touches entries you typed yourself.

## Building

Needs Qt 6.2 or newer (Widgets, Sql, Network, Test) and CMake 3.19.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/workcalendar
```

Debian/Ubuntu: `apt install qt6-base-dev qt6-base-dev-tools libqt6sql6-sqlite cmake g++`

The SQLite driver is a separate package on most distributions; without it the
application starts and reports that it cannot open its database.

### Where the data lives

| What        | Linux                                              |
| ----------- | -------------------------------------------------- |
| work log    | `~/.local/share/WorkCalendar/WorkCalendar/worklog.sqlite` |
| preferences | `~/.config/WorkCalendar/WorkCalendar.ini`          |

*Help > About* shows the exact path. Back up the `.sqlite` file and you have
backed up everything. `--database <file>` opens a different one, which is handy
for a second customer or for trying something out.

## Keyboard

| | |
| --- | --- |
| `Ctrl+N` / `Ctrl+E` / `Ctrl+D` / `Del` | new, edit, duplicate, delete entry |
| `Ctrl+T` | day type |
| `Ctrl+Shift+S` / `Ctrl+Shift+X` | start / stop the timer |
| `Ctrl+Shift+C` | copy the stand-up note for the selected day |
| `Ctrl+Shift+E` | export to Excel |
| arrows, `PgUp`/`PgDn`, `Home`/`End`, `Enter` | move around the calendar, open a day |
| `F5` | reload |

## Durations

Type them however you think of them: `2h30`, `2.5`, `2,5`, `150m`, `2:30`, `2h 30m`.
They are stored as whole minutes, so a month of quarter hours adds up exactly.
*Settings > New entries* can round every new duration up to a fixed increment.

## Jira import

*Settings > Jira*: server, user name, API token, and three fields that describe
your server's conventions - the sprint custom field (`customfield_10005` on
most installations), the word that marks a testsheet attachment, and the URL
fragment that marks a merge request remote link.

The import searches for the issues you booked time on in the period, then for
each one reads its remote links and its worklogs, keeps the worklogs that are
yours, and builds an entry per worklog carrying the issue's summary, type,
priority, components, fix versions, sprint, testsheet and merge request. Each
entry remembers the Jira worklog it came from, which is what makes re-running a
period safe.

The token is stored in the settings file in clear text, like any `.ini`. Leave
it empty to be asked at each import instead.

## Layout

```
src/
  core/      WorkEntry, DayMeta, DaySummary, the enums, durations, settings
  data/      SQLite: schema and migrations, the two repositories, statistics
  model/     the table models and the filter proxy
  widgets/   MonthGridWidget and HoursBarChart - the two custom painted views
  export/    XlsxWriter, the three-sheet report, CSV, the text renderings
  jira/      the REST client
  ui/        one .ui file and one class per window or dialog
tests/       six QtTest binaries
tools/       validate_xlsx.py - opens an exported workbook with openpyxl
```

Reading it from the bottom up works well: `core` knows nothing about storage,
`data` knows nothing about widgets, and `MainWindow` is the only thing that
talks to a repository.

## House rules

These were the constraints the code was written under, and they are worth
keeping:

- **Every widget comes from a `.ui` file.** Nothing builds a widget to display
  data. The month grid and the bar chart are promoted widgets, so they sit in
  Qt Designer like any other; rows are shown by models and item data. The only
  widgets created in code are the two context menus, and they only ever hold
  actions that are already in `MainWindow.ui`.
- **No lambdas.** Every `connect()` names a slot you can search for, set a
  breakpoint in, and read on its own.
- **One place per fact.** A new column is one row in the table at the top of
  `WorkEntry.cpp`; the table, the CSV, the Excel export and the columns dialog
  all pick it up. A new setting is one getter, one setter and one documented
  default in `Settings`.
- **Minutes, not hours.** Durations are integers everywhere.
- **Failures come back as sentences.** Every operation that can fail takes a
  `QString *error` and fills it with something worth showing a person.

## Excel without a library

`src/export/XlsxWriter.cpp` writes the `.xlsx` directly: the OOXML parts it
needs, in a zip container built in the same file. It covers text, numbers,
fonts, fills, wrapped text, borders, number formats, column widths, row
heights, merged cells, frozen headers, autofilter and external hyperlinks.

Entries are stored rather than deflated, which is legal, universally readable,
and keeps the whole thing to one file with no compression dependency. A month
of work produces a file of a few tens of kilobytes.

`tools/validate_xlsx.py` opens a produced file with `openpyxl` and prints what
it found, for when you change the writer and want a second opinion from a real
spreadsheet library.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

| | |
| --- | --- |
| `tst_duration` | every spelling of a duration people type |
| `tst_repository` | storage against real SQLite, in memory and on disk |
| `tst_statistics` | targets, day states, balances, streaks, breakdowns |
| `tst_csv` | round trips, including commas, quotes and newlines in fields |
| `tst_xlsx` | the zip container, the cell references, the whole report |
| `tst_mainwindow` | builds the real window over a real database and drives it |

`tst_mainwindow` runs on the `offscreen` platform, so the suite needs no
display.

## Known limits

- **The Jira import blocks while it runs.** It is a modal dialog doing one
  request at a time, with a progress bar and a Stop button; it has not been
  run against every Jira version, and a server whose sprint field or attachment
  naming differs needs those three settings adjusted.
- **No undo.** Deletions ask first, and that is all.
- **One user, one file.** There is no sharing, no sync and no multi-user
  locking; two copies pointed at the same file over a network share will not
  end well.
- **The work log is not a timesheet system.** It does not submit anything
  anywhere - it produces the spreadsheet, and a person takes it from there.
