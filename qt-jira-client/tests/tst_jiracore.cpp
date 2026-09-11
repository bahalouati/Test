#include "core/credentials.h"
#include "core/jiraclient.h"
#include "core/jiratypes.h"
#include "core/timesheet.h"
#include "core/tasktracker.h"
#include "core/timesheetexport.h"
#include "core/timesheetloader.h"
#include "core/xlsxwriter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

// Covers the parts that are easy to get wrong and expensive to debug against a
// live server: URL construction, the auth header, Jira's timestamp format, and
// the shape of the payloads we read. No network, no credentials.
class TestJiraCore : public QObject
{
    Q_OBJECT

private slots:
    void normalizesBaseUrls_data();
    void normalizesBaseUrls();
    void buildsRestEndpoints();
    void preservesContextPath();
    void buildsAuthorizationHeaders();
    void rejectsIncompleteCredentials();
    void parsesJiraTimestamps_data();
    void parsesJiraTimestamps();
    void roundTripsWorklogTimestamps();
    void formatsDurations();
    void parsesSearchResults();
    void parsesIssueWithNullFields();
    void parsesComments();
    void parsesWorklogs();
    void parsesTransitions();
    void parsesProjectsFromBothShapes();
    void readsJiraErrorBodies();
    void explainsAuthenticationFailures();
    void sortsIssueKeysNumerically();
    void parsesSprintFromServerToString();
    void parsesSprintFromModernObject();
    void parsesFixVersionsAndAttachments();
    void findsTestSheetAttachment();
    void findsMergeRequestLink();
    void classifiesDaysAgainstTarget();
    void countsMissingHoursPerDay();
    void includesDaysWithNothingLogged();
    void buildsWorklogAuthorJql();
    void formatsCalendarLine();
    void namesSpreadsheetColumns();
    void writesAReadableWorkbook();
    void writesCsvWithABomAndTheRightColumns();
    void formatsTrackedTime();
    void runsOneTimerAtATime();
    void dropsTasksNoLongerAssignedToMe();
    void keepsUnassignedTasksThatHoldTime();
    void recognisesSpecificationFixVersions();
    void holidaysOweNothing();
    void holidaysLeaveTheMonthTotalAlone();
    void persistsHolidaysAcrossLoads();
    void validatesWorklogDurations_data();
    void validatesWorklogDurations();
};

void TestJiraCore::normalizesBaseUrls_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("plain") << "https://jira.example.com" << "https://jira.example.com";
    QTest::newRow("trailing slash") << "https://jira.example.com/" << "https://jira.example.com";
    QTest::newRow("many slashes") << "https://jira.example.com///" << "https://jira.example.com";
    QTest::newRow("no scheme") << "jira.example.com" << "https://jira.example.com";
    QTest::newRow("http kept") << "http://jira.internal:8080" << "http://jira.internal:8080";
    QTest::newRow("whitespace") << "  https://jira.example.com  " << "https://jira.example.com";
    QTest::newRow("context path") << "https://intranet.example.com/jira/" << "https://intranet.example.com/jira";
    QTest::newRow("pasted rest root") << "https://jira.example.com/rest/api/2" << "https://jira.example.com";
    QTest::newRow("pasted dashboard")
            << "https://jira.example.com/secure/Dashboard.jspa" << "https://jira.example.com";
    QTest::newRow("empty") << "" << "";
}

void TestJiraCore::normalizesBaseUrls()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(jira::normalizeBaseUrl(input), expected);
}

void TestJiraCore::buildsRestEndpoints()
{
    QCOMPARE(jira::restEndpoint(QStringLiteral("https://jira.example.com"), QStringLiteral("myself")).toString(),
             QStringLiteral("https://jira.example.com/rest/api/2/myself"));

    // A leading slash on the resource must not double up.
    QCOMPARE(jira::restEndpoint(QStringLiteral("https://jira.example.com"), QStringLiteral("/search")).toString(),
             QStringLiteral("https://jira.example.com/rest/api/2/search"));

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("100"));
    QCOMPARE(jira::restEndpoint(QStringLiteral("https://jira.example.com"),
                                QStringLiteral("issue/OPS-1/comment"),
                                query)
                     .toString(),
             QStringLiteral("https://jira.example.com/rest/api/2/issue/OPS-1/comment?maxResults=100"));

    QVERIFY(!jira::restEndpoint(QString(), QStringLiteral("myself")).isValid()
            || jira::restEndpoint(QString(), QStringLiteral("myself")).host().isEmpty());
}

void TestJiraCore::preservesContextPath()
{
    // Self-hosted Jira is routinely mounted under a path; losing it turns every
    // call into a 404 against the front-end web server.
    QCOMPARE(jira::restEndpoint(QStringLiteral("https://intranet.example.com/jira"),
                                QStringLiteral("issue/OPS-7"))
                     .toString(),
             QStringLiteral("https://intranet.example.com/jira/rest/api/2/issue/OPS-7"));
}

void TestJiraCore::buildsAuthorizationHeaders()
{
    jira::Credentials cloud;
    cloud.mode = jira::AuthMode::Basic;
    cloud.username = QStringLiteral("someone@example.com");
    cloud.token = QStringLiteral("secret-token");
    QCOMPARE(cloud.authorizationHeader(),
             QByteArray("Basic ") + QByteArray("someone@example.com:secret-token").toBase64());

    jira::Credentials server;
    server.mode = jira::AuthMode::Bearer;
    server.token = QStringLiteral("  pat-token  ");
    QCOMPARE(server.authorizationHeader(), QByteArray("Bearer pat-token"));

    jira::Credentials empty;
    QVERIFY(empty.authorizationHeader().isEmpty());
}

void TestJiraCore::rejectsIncompleteCredentials()
{
    jira::Credentials credentials;
    QVERIFY(!credentials.isComplete());

    credentials.baseUrl = QStringLiteral("https://jira.example.com");
    QVERIFY(!credentials.isComplete());   // no token

    credentials.token = QStringLiteral("t");
    credentials.mode = jira::AuthMode::Basic;
    QVERIFY(!credentials.isComplete());   // Basic still needs the e-mail

    credentials.mode = jira::AuthMode::Bearer;
    QVERIFY(credentials.isComplete());    // a PAT stands alone

    credentials.mode = jira::AuthMode::Basic;
    credentials.username = QStringLiteral("someone@example.com");
    QVERIFY(credentials.isComplete());
}

void TestJiraCore::parsesJiraTimestamps_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expectedUtc");

    // Every row is the same instant written the way a different Jira sends it.
    QTest::newRow("offset without colon") << "2026-09-07T10:30:00.000+0000" << "2026-09-07T10:30:00Z";
    QTest::newRow("positive offset") << "2026-09-07T12:30:00.000+0200" << "2026-09-07T10:30:00Z";
    QTest::newRow("negative offset") << "2026-09-07T06:30:00.000-0400" << "2026-09-07T10:30:00Z";
    QTest::newRow("no milliseconds") << "2026-09-07T10:30:00+0000" << "2026-09-07T10:30:00Z";
}

void TestJiraCore::parsesJiraTimestamps()
{
    QFETCH(QString, input);
    QFETCH(QString, expectedUtc);

    const QDateTime parsed = jira::parseDateTime(input);
    QVERIFY2(parsed.isValid(), qPrintable(input));
    QCOMPARE(parsed.toUTC().toString(Qt::ISODate), expectedUtc);

    QVERIFY(!jira::parseDateTime(QString()).isValid());
    QVERIFY(!jira::parseDateTime(QStringLiteral("not a date")).isValid());
}

void TestJiraCore::roundTripsWorklogTimestamps()
{
    // Jira rejects a worklog "started" whose offset carries a colon, so the
    // formatter has to undo what Qt::ISODateWithMs produces.
    const QDateTime started = QDateTime::fromString(QStringLiteral("2026-09-07T09:15:00.000+02:00"),
                                                    Qt::ISODateWithMs);
    QVERIFY(started.isValid());
    const QString wire = jira::formatDateTime(started);

    QVERIFY2(wire.endsWith(QStringLiteral("+0200")), qPrintable(wire));
    QVERIFY(!wire.contains(QStringLiteral("+02:00")));
    QCOMPARE(jira::parseDateTime(wire).toUTC(), started.toUTC());

    QVERIFY(jira::formatDateTime(QDateTime()).isEmpty());
}

void TestJiraCore::formatsDurations()
{
    QCOMPARE(jira::formatDuration(3600), QStringLiteral("1h"));
    QCOMPARE(jira::formatDuration(5400), QStringLiteral("1h 30m"));
    QCOMPARE(jira::formatDuration(900), QStringLiteral("15m"));
    QCOMPARE(jira::formatDuration(0), QStringLiteral("-"));
    QCOMPARE(jira::formatDuration(-10), QStringLiteral("-"));
}

void TestJiraCore::parsesSearchResults()
{
    const QByteArray payload = R"({
        "startAt": 50,
        "maxResults": 50,
        "total": 137,
        "issues": [
            {
                "id": "10001",
                "key": "OPS-42",
                "fields": {
                    "summary": "Rotate the signing certificate",
                    "description": "It expires on Friday.",
                    "status": {"name": "In Progress", "statusCategory": {"key": "indeterminate"}},
                    "issuetype": {"name": "Task"},
                    "priority": {"name": "High"},
                    "project": {"key": "OPS", "name": "Operations"},
                    "assignee": {"name": "jdoe", "displayName": "J. Doe", "emailAddress": "jdoe@example.com"},
                    "reporter": {"displayName": "A. Smith"},
                    "labels": ["security", "urgent"],
                    "components": [{"name": "Platform"}],
                    "created": "2026-09-01T08:00:00.000+0000",
                    "updated": "2026-09-07T10:30:00.000+0000",
                    "duedate": "2026-09-11",
                    "timespent": 7200,
                    "timeoriginalestimate": 14400
                }
            }
        ]
    })";

    const jira::SearchResult result = jira::SearchResult::fromJson(QJsonDocument::fromJson(payload).object());
    QCOMPARE(result.startAt, 50);
    QCOMPARE(result.total, 137);
    QCOMPARE(result.issues.size(), 1);

    const jira::Issue &issue = result.issues.first();
    QCOMPARE(issue.key, QStringLiteral("OPS-42"));
    QCOMPARE(issue.summary, QStringLiteral("Rotate the signing certificate"));
    QCOMPARE(issue.status, QStringLiteral("In Progress"));
    QCOMPARE(issue.statusCategory, QStringLiteral("indeterminate"));
    QVERIFY(!issue.isResolved());
    QCOMPARE(issue.issueType, QStringLiteral("Task"));
    QCOMPARE(issue.priority, QStringLiteral("High"));
    QCOMPARE(issue.projectKey, QStringLiteral("OPS"));
    QCOMPARE(issue.assignee.label(), QStringLiteral("J. Doe"));
    QCOMPARE(issue.reporter.label(), QStringLiteral("A. Smith"));
    QCOMPARE(issue.labels, QStringList({QStringLiteral("security"), QStringLiteral("urgent")}));
    QCOMPARE(issue.components, QStringList({QStringLiteral("Platform")}));
    QCOMPARE(issue.dueDate, QDate(2026, 9, 11));
    QCOMPARE(issue.timeSpentSeconds, 7200);
    QCOMPARE(issue.originalEstimateSeconds, 14400);
    QVERIFY(issue.updated.isValid());
}

void TestJiraCore::parsesIssueWithNullFields()
{
    // Jira sends explicit nulls for an unassigned, unprioritised issue; those
    // must read as empty rather than crash or invent a value.
    const QByteArray payload = R"({
        "key": "OPS-1",
        "fields": {
            "summary": "Bare issue",
            "assignee": null,
            "priority": null,
            "resolution": null,
            "labels": [],
            "components": null,
            "description": null,
            "status": {"name": "Done", "statusCategory": {"key": "done"}}
        }
    })";

    const jira::Issue issue = jira::Issue::fromJson(QJsonDocument::fromJson(payload).object());
    QCOMPARE(issue.key, QStringLiteral("OPS-1"));
    QVERIFY(issue.assignee.isNull());
    QCOMPARE(issue.assignee.label(), QStringLiteral("Unassigned"));
    QVERIFY(issue.priority.isEmpty());
    QVERIFY(issue.labels.isEmpty());
    QVERIFY(issue.components.isEmpty());
    QVERIFY(issue.description.isEmpty());
    QVERIFY(issue.isResolved());

    // "total" absent: fall back to what actually arrived rather than reporting 0.
    const jira::SearchResult result = jira::SearchResult::fromJson(
            QJsonDocument::fromJson(R"({"startAt": 0, "issues": [{"key": "OPS-1"}]})").object());
    QCOMPARE(result.total, 1);
}

void TestJiraCore::parsesComments()
{
    const QByteArray payload = R"({
        "comments": [
            {
                "id": "9001",
                "author": {"displayName": "J. Doe"},
                "body": "Certificate renewed.",
                "created": "2026-09-07T11:00:00.000+0000",
                "updated": "2026-09-07T11:05:00.000+0000"
            }
        ]
    })";

    const QList<jira::Comment> comments = jira::Comment::listFromJson(QJsonDocument::fromJson(payload).object());
    QCOMPARE(comments.size(), 1);
    QCOMPARE(comments.first().author.label(), QStringLiteral("J. Doe"));
    // v2 hands back a plain string here; v3 would hand back an ADF document.
    QCOMPARE(comments.first().body, QStringLiteral("Certificate renewed."));
    QVERIFY(comments.first().created.isValid());

    QVERIFY(jira::Comment::listFromJson(QJsonObject()).isEmpty());
}

void TestJiraCore::parsesWorklogs()
{
    const QByteArray payload = R"({
        "worklogs": [
            {
                "id": "5001",
                "author": {"displayName": "J. Doe"},
                "comment": "Renewal and deployment",
                "started": "2026-09-07T09:00:00.000+0000",
                "timeSpentSeconds": 5400,
                "timeSpent": "1h 30m"
            },
            {
                "id": "5002",
                "author": {"name": "asmith"},
                "started": "2026-09-07T13:00:00.000+0000",
                "timeSpentSeconds": 1800
            }
        ]
    })";

    const QList<jira::Worklog> worklogs = jira::Worklog::listFromJson(QJsonDocument::fromJson(payload).object());
    QCOMPARE(worklogs.size(), 2);
    QCOMPARE(worklogs.first().timeSpent, QStringLiteral("1h 30m"));
    QCOMPARE(worklogs.first().timeSpentSeconds, 5400);
    // No "timeSpent" string on the second entry, so it is derived.
    QCOMPARE(worklogs.at(1).timeSpent, QStringLiteral("30m"));
    QCOMPARE(worklogs.at(1).author.label(), QStringLiteral("asmith"));
}

void TestJiraCore::parsesTransitions()
{
    const QByteArray payload = R"({
        "transitions": [
            {"id": "21", "name": "Start Progress", "to": {"name": "In Progress"}},
            {"id": "31", "name": "Done", "to": {"name": "Done"}},
            {"name": "Broken entry without an id"}
        ]
    })";

    const QList<jira::Transition> transitions =
            jira::Transition::listFromJson(QJsonDocument::fromJson(payload).object());
    QCOMPARE(transitions.size(), 2);   // the id-less entry is dropped
    QCOMPARE(transitions.first().id, QStringLiteral("21"));
    QCOMPARE(transitions.first().name, QStringLiteral("Start Progress"));
    QCOMPARE(transitions.first().toStatus, QStringLiteral("In Progress"));
}

void TestJiraCore::parsesProjectsFromBothShapes()
{
    const QJsonDocument bare = QJsonDocument::fromJson(R"([{"id": "1", "key": "OPS", "name": "Operations"}])");
    QCOMPARE(jira::Project::listFromJson(bare.array()).size(), 1);
    QCOMPARE(jira::Project::listFromJson(bare.array()).first().key, QStringLiteral("OPS"));

    const QJsonDocument paged =
            QJsonDocument::fromJson(R"({"values": [{"key": "OPS"}, {"key": "PLAT"}, {"name": "no key"}]})");
    QCOMPARE(jira::Project::listFromJson(paged.object()).size(), 2);
}

void TestJiraCore::readsJiraErrorBodies()
{
    const QByteArray body = R"({
        "errorMessages": ["Field 'nosuchfield' does not exist or you do not have permission to view it."],
        "errors": {"timeSpent": "Time spent must not be empty."}
    })";

    const jira::Error error = jira::Error::fromResponse(400, body, QString());
    QCOMPARE(error.httpStatus, 400);
    QVERIFY(!error.isAuthenticationFailure());
    QCOMPARE(error.details.size(), 1);   // the first message becomes the headline
    QVERIFY(error.message.contains(QStringLiteral("nosuchfield")));
    QVERIFY(error.toString().contains(QStringLiteral("Time spent must not be empty.")));
}

void TestJiraCore::explainsAuthenticationFailures()
{
    const jira::Error unauthorised = jira::Error::fromResponse(401, QByteArray(), QString());
    QVERIFY(unauthorised.isAuthenticationFailure());
    QVERIFY(unauthorised.message.contains(QStringLiteral("401")));

    const jira::Error forbidden = jira::Error::fromResponse(403, QByteArray(), QString());
    QVERIFY(forbidden.isAuthenticationFailure());

    // 404 is nearly always the base URL, so the message says so.
    const jira::Error notFound = jira::Error::fromResponse(404, QByteArray(), QString());
    QVERIFY(notFound.message.contains(QStringLiteral("URL")));

    // No body and no status: fall back to the transport error.
    const jira::Error offline = jira::Error::fromResponse(0, QByteArray(), QStringLiteral("Host not found"));
    QCOMPARE(offline.message, QStringLiteral("Host not found"));
}

void TestJiraCore::sortsIssueKeysNumerically()
{
    QStringList keys = {QStringLiteral("OPS-10"), QStringLiteral("OPS-9"), QStringLiteral("OPS-100"),
                        QStringLiteral("PLAT-2")};
    std::sort(keys.begin(), keys.end(), [](const QString &a, const QString &b) {
        return jira::issueSortKey(a) < jira::issueSortKey(b);
    });
    QCOMPARE(keys,
             QStringList({QStringLiteral("OPS-9"), QStringLiteral("OPS-10"), QStringLiteral("OPS-100"),
                          QStringLiteral("PLAT-2")}));

    // Anything that is not key-shaped is left alone rather than mangled.
    QCOMPARE(jira::issueSortKey(QStringLiteral("NOTAKEY")), QStringLiteral("NOTAKEY"));
    QCOMPARE(jira::issueSortKey(QStringLiteral("OPS-abc")), QStringLiteral("OPS-abc"));
    QCOMPARE(jira::issueSortKey(QString()), QString());
}

void TestJiraCore::parsesSprintFromServerToString()
{
    // What Jira Server actually returns: the Java toString() of the sprint.
    const QByteArray payload = R"({
        "customfield_10005": [
            "com.atlassian.greenhopper.service.sprint.Sprint@1a[id=41,rapidViewId=7,state=CLOSED,name=Sprint 11,startDate=2026-08-01]",
            "com.atlassian.greenhopper.service.sprint.Sprint@2b[id=42,rapidViewId=7,state=ACTIVE,name=Sprint 12,startDate=2026-09-01]"
        ]
    })";
    const QJsonObject fields = QJsonDocument::fromJson(payload).object();

    // The last entry is the current sprint, not the first.
    QCOMPARE(jira::sprintNameFromField(fields.value(QStringLiteral("customfield_10005"))),
             QStringLiteral("Sprint 12"));

    // A name that runs to the closing bracket rather than a comma.
    const QJsonDocument tail = QJsonDocument::fromJson(
            R"({"f": ["com.atlassian.greenhopper.service.sprint.Sprint@3c[id=9,name=Hardening]"]})");
    QCOMPARE(jira::sprintNameFromField(tail.object().value(QStringLiteral("f"))),
             QStringLiteral("Hardening"));

    QVERIFY(jira::sprintNameFromField(QJsonValue()).isEmpty());
    QVERIFY(jira::sprintNameFromField(QJsonValue(QJsonArray())).isEmpty());
}

void TestJiraCore::parsesSprintFromModernObject()
{
    // Newer instances return real objects; the same call must cope.
    const QJsonDocument document = QJsonDocument::fromJson(
            R"({"f": [{"id": 41, "name": "Sprint 11"}, {"id": 42, "name": "Sprint 12"}]})");
    QCOMPARE(jira::sprintNameFromField(document.object().value(QStringLiteral("f"))),
             QStringLiteral("Sprint 12"));
}

void TestJiraCore::parsesFixVersionsAndAttachments()
{
    const QByteArray payload = R"({
        "key": "OPS-42",
        "fields": {
            "summary": "Rotate the signing certificate",
            "fixVersions": [{"name": "1.4.0"}, {"name": "1.5.0"}],
            "attachment": [
                {"id": "1", "filename": "notes.txt", "content": "https://jira/secure/attachment/1/notes.txt"},
                {"id": "2", "filename": "OPS-42_TestSheet_v3.xlsx",
                 "content": "https://jira/secure/attachment/2/sheet.xlsx", "size": 8192}
            ],
            "customfield_10005": ["com.x.Sprint@1[id=42,name=Sprint 12,state=ACTIVE]"]
        }
    })";

    const jira::Issue issue = jira::Issue::fromJson(QJsonDocument::fromJson(payload).object());
    QCOMPARE(issue.fixVersions, QStringList({QStringLiteral("1.4.0"), QStringLiteral("1.5.0")}));
    QCOMPARE(issue.attachments.size(), 2);
    QCOMPARE(issue.attachments.at(1).size, qint64(8192));
    // The sprint field id is not hard-coded; any customfield_* that parses wins.
    QCOMPARE(issue.sprint, QStringLiteral("Sprint 12"));
}

void TestJiraCore::findsTestSheetAttachment()
{
    const QJsonDocument document = QJsonDocument::fromJson(R"([
        {"filename": "screenshot.png", "content": "https://jira/a/1"},
        {"filename": "OPS-42_TESTSHEET_final.xlsx", "content": "https://jira/a/2"},
        {"filename": "another_testsheet.xlsx", "content": "https://jira/a/3"}
    ])");
    const QList<jira::Attachment> attachments = jira::Attachment::listFromJson(document.array());
    QCOMPARE(attachments.size(), 3);

    // Case-insensitive, and the first match wins -- same as the script.
    const jira::Attachment sheet = jira::findAttachment(attachments, QStringLiteral("testsheet"));
    QCOMPARE(sheet.filename, QStringLiteral("OPS-42_TESTSHEET_final.xlsx"));
    QCOMPARE(sheet.contentUrl, QStringLiteral("https://jira/a/2"));

    QVERIFY(jira::findAttachment(attachments, QStringLiteral("specification")).isNull());
    // An empty marker must not match everything by accident.
    QVERIFY(jira::findAttachment(attachments, QString()).isNull());
}

void TestJiraCore::findsMergeRequestLink()
{
    const QJsonDocument document = QJsonDocument::fromJson(R"([
        {"object": {"url": "https://wiki.example.com/page", "title": "Design"}},
        {"object": {"url": "https://gitlab.example.com/team/app/-/merge_requests/812", "title": "MR 812"}}
    ])");
    const QList<jira::RemoteLink> links = jira::RemoteLink::listFromJson(document.array());
    QCOMPARE(links.size(), 2);
    QCOMPARE(jira::findLinkUrl(links, QStringLiteral("/merge_requests/")),
             QStringLiteral("https://gitlab.example.com/team/app/-/merge_requests/812"));
    QVERIFY(jira::findLinkUrl(links, QStringLiteral("/pull/")).isEmpty());
}

void TestJiraCore::classifiesDaysAgainstTarget()
{
    jira::TimesheetRules rules;   // 8h full, 6h amber
    const QDate today(2026, 9, 11);

    QCOMPARE(jira::classifyDay(QDate(2026, 9, 10), 8.0, today, rules), jira::DayStatus::Complete);
    QCOMPARE(jira::classifyDay(QDate(2026, 9, 10), 9.5, today, rules), jira::DayStatus::Complete);
    QCOMPARE(jira::classifyDay(QDate(2026, 9, 10), 6.0, today, rules), jira::DayStatus::Partial);
    QCOMPARE(jira::classifyDay(QDate(2026, 9, 10), 7.9, today, rules), jira::DayStatus::Partial);
    QCOMPARE(jira::classifyDay(QDate(2026, 9, 10), 5.9, today, rules), jira::DayStatus::Short);
    QCOMPARE(jira::classifyDay(QDate(2026, 9, 10), 0.0, today, rules), jira::DayStatus::Short);

    // Today itself is judged like any other day, but tomorrow is not judged at all.
    QCOMPARE(jira::classifyDay(today, 0.0, today, rules), jira::DayStatus::Short);
    QCOMPARE(jira::classifyDay(QDate(2026, 9, 14), 0.0, today, rules), jira::DayStatus::Future);

    QVERIFY(rules.isWorkingDay(QDate(2026, 9, 11)));    // Friday
    QVERIFY(!rules.isWorkingDay(QDate(2026, 9, 12)));   // Saturday
    QVERIFY(!rules.isWorkingDay(QDate(2026, 9, 13)));   // Sunday
}

void TestJiraCore::countsMissingHoursPerDay()
{
    jira::TimesheetRules rules;
    const QDate today(2026, 9, 11);

    QList<jira::TimesheetEntry> entries;
    const auto entry = [](const QDate &day, const QString &key, double hours) {
        jira::TimesheetEntry e;
        e.day = day;
        e.issueKey = key;
        e.hours = hours;
        return e;
    };
    // Mon full, Tue short by 2.5, Wed nothing at all, Thu over a full day.
    entries << entry(QDate(2026, 9, 7), QStringLiteral("OPS-1"), 8.0)
            << entry(QDate(2026, 9, 8), QStringLiteral("OPS-2"), 3.5)
            << entry(QDate(2026, 9, 8), QStringLiteral("OPS-3"), 2.0)
            << entry(QDate(2026, 9, 10), QStringLiteral("OPS-4"), 9.0);

    const QList<jira::DaySummary> days = jira::summariseDays(
            entries, QDate(2026, 9, 7), QDate(2026, 9, 11), today, rules);
    QCOMPARE(days.size(), 5);   // Mon-Fri

    QCOMPARE(days.at(0).hours, 8.0);
    QVERIFY(!days.at(0).isMissing(rules));

    QCOMPARE(days.at(1).hours, 5.5);            // two worklogs summed
    QCOMPARE(days.at(1).entries.size(), 2);
    QCOMPARE(days.at(1).missingHours(rules), 2.5);

    QCOMPARE(days.at(2).hours, 0.0);            // Wednesday, nothing logged
    QCOMPARE(days.at(2).missingHours(rules), 8.0);

    // Over a full day owes nothing; it must not offset another day either.
    QCOMPARE(days.at(3).missingHours(rules), 0.0);

    // Friday is today with nothing logged -> still counted as missing.
    QCOMPARE(days.at(4).missingHours(rules), 8.0);

    QCOMPARE(jira::totalLoggedHours(days), 22.5);
    QCOMPARE(jira::totalMissingHours(days, rules), 18.5);   // 2.5 + 8 + 8
}

void TestJiraCore::includesDaysWithNothingLogged()
{
    jira::TimesheetRules rules;
    // An empty month still yields one row per weekday -- the empty days are
    // exactly what the view exists to show.
    const QList<jira::DaySummary> days = jira::summariseDays(
            {}, QDate(2026, 9, 1), QDate(2026, 9, 30), QDate(2026, 9, 30), rules);
    QCOMPARE(days.size(), 22);   // weekdays in September 2026
    for (const jira::DaySummary &day : days)
        QCOMPARE(day.status, jira::DayStatus::Short);

    // A reversed or invalid range yields nothing rather than looping forever.
    QVERIFY(jira::summariseDays({}, QDate(2026, 9, 30), QDate(2026, 9, 1),
                                QDate(2026, 9, 30), rules).isEmpty());
    QVERIFY(jira::summariseDays({}, QDate(), QDate(), QDate(), rules).isEmpty());
}

void TestJiraCore::buildsWorklogAuthorJql()
{
    jira::User server;
    server.name = QStringLiteral("mlouati");
    QCOMPARE(jira::TimesheetLoader::buildJql(QDate(2026, 9, 1), QDate(2026, 9, 30), server),
             QStringLiteral("worklogAuthor = \"mlouati\" AND worklogDate >= \"2026-09-01\" "
                            "AND worklogDate <= \"2026-09-30\""));

    jira::User cloud;
    cloud.accountId = QStringLiteral("5b10a2");
    QVERIFY(jira::TimesheetLoader::buildJql(QDate(2026, 9, 1), QDate(2026, 9, 30), cloud)
                    .contains(QStringLiteral("worklogAuthor = \"5b10a2\"")));

    // Nothing known about the user: fall back to Jira resolving it.
    QVERIFY(jira::TimesheetLoader::buildJql(QDate(2026, 9, 1), QDate(2026, 9, 30), jira::User())
                    .startsWith(QStringLiteral("worklogAuthor = currentUser()")));

    // A quote in a user name must not break out of the JQL string.
    jira::User awkward;
    awkward.name = QStringLiteral("o\"brien");
    QVERIFY(jira::TimesheetLoader::buildJql(QDate(2026, 9, 1), QDate(2026, 9, 30), awkward)
                    .contains(QStringLiteral("\"o\\\"brien\"")));
}

void TestJiraCore::formatsCalendarLine()
{
    jira::TimesheetEntry entry;
    entry.issueKey = QStringLiteral("OPS-42");
    entry.sprint = QStringLiteral("Sprint 12");
    entry.fixVersions = QStringLiteral("1.4.0");
    entry.hours = 2.5;
    QCOMPARE(entry.calendarLine(), QStringLiteral("OPS-42 [Sprint 12] [1.4.0] (2.5h)"));

    // Whole hours lose the decimal; missing values show a dash, as in the script.
    jira::TimesheetEntry bare;
    bare.issueKey = QStringLiteral("OPS-1");
    bare.hours = 3.0;
    QCOMPARE(bare.calendarLine(), QStringLiteral("OPS-1 [-] [-] (3h)"));
    QVERIFY(!bare.hasFixVersion());
    QVERIFY(!bare.hasSprint());
    QVERIFY(!bare.hasTestSheet());
    QVERIFY(!bare.hasMergeRequest());
}

void TestJiraCore::namesSpreadsheetColumns()
{
    QCOMPARE(xlsx::Sheet::columnName(1), QStringLiteral("A"));
    QCOMPARE(xlsx::Sheet::columnName(26), QStringLiteral("Z"));
    QCOMPARE(xlsx::Sheet::columnName(27), QStringLiteral("AA"));
    QCOMPARE(xlsx::Sheet::columnName(52), QStringLiteral("AZ"));
    QCOMPARE(xlsx::Sheet::columnName(53), QStringLiteral("BA"));
    QCOMPARE(xlsx::Sheet::cellReference(7, 1), QStringLiteral("A7"));
    QCOMPARE(xlsx::Sheet::cellReference(1, 28), QStringLiteral("AB1"));
}

void TestJiraCore::writesAReadableWorkbook()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("book.xlsx"));

    jira::TimesheetEntry entry;
    entry.day = QDate(2026, 9, 8);
    entry.issueKey = QStringLiteral("CADIM-101");
    entry.summary = QStringLiteral("Rotate the <signing> certificate & key");   // XML-hostile
    entry.fixVersions = QStringLiteral("1.4.0");
    entry.sprint = QStringLiteral("Sprint 12");
    entry.mergeRequestUrl = QStringLiteral("https://gitlab.example.com/a/b/-/merge_requests/8");
    entry.testSheetName = QStringLiteral("CADIM-101_TestSheet.xlsx");
    entry.testSheetUrl = QStringLiteral("https://jira.example.com/secure/attachment/10/s.xlsx");
    entry.hours = 6.0;

    jira::TimesheetRules rules;
    const QList<jira::DaySummary> days = jira::summariseDays(
            {entry}, QDate(2026, 9, 1), QDate(2026, 9, 30), QDate(2026, 9, 11), rules);

    QString error;
    QVERIFY2(jira::exportTimesheetWorkbook(path, {entry}, days, QDate(2026, 9, 1), rules, &error),
             qPrintable(error));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray archive = file.readAll();
    file.close();

    // A ZIP container whose first entry is the part Excel looks for first.
    QVERIFY(archive.size() > 1000);
    QVERIFY(archive.startsWith("PK\x03\x04"));
    QVERIFY(archive.contains("[Content_Types].xml"));
    QVERIFY(archive.contains("xl/worksheets/sheet1.xml"));
    QVERIFY(archive.contains("xl/worksheets/sheet2.xml"));
    QVERIFY(archive.contains("xl/styles.xml"));
    // The end-of-central-directory record has to be there or nothing can open it.
    QVERIFY(archive.contains("PK\x05\x06"));

    // The hostile summary must have been escaped rather than written raw.
    QVERIFY(archive.contains("Rotate the &lt;signing&gt; certificate &amp; key"));
    QVERIFY(!archive.contains("<signing>"));

    QCOMPARE(jira::suggestedWorkbookName(QDate(2026, 9, 1)),
             QStringLiteral("Jira_Worklog_Calendar_2026_09.xlsx"));
}

void TestJiraCore::writesCsvWithABomAndTheRightColumns()
{
    jira::TimesheetEntry entry;
    entry.day = QDate(2026, 9, 8);
    entry.issueKey = QStringLiteral("CADIM-101");
    entry.summary = QStringLiteral("Rotate the \"signing\" certificate");   // a quote to escape
    entry.hours = 6.0;
    entry.sprint = QStringLiteral("Sprint 12");
    entry.fixVersions = QStringLiteral("1.4.0");
    entry.specifications = QStringLiteral("P221997");
    entry.mergeRequestUrl = QStringLiteral("https://gitlab.example.com/a/-/merge_requests/8");
    entry.testSheetName = QStringLiteral("sheet.xlsx");
    entry.testSheetUrl = QStringLiteral("https://jira.example.com/a/10");
    entry.comment = QStringLiteral("Cert follow-up");

    const QByteArray csv = jira::buildTimesheetCsv({entry});

    // Without the BOM Excel reads the file as the system code page.
    QVERIFY(csv.startsWith("\xEF\xBB\xBF"));

    const QStringList lines = QString::fromUtf8(csv.mid(3)).split(QLatin1Char('\n'),
                                                                 Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines.first(),
             QStringLiteral("Date,Issue,Summary,Hours,Sprint,Fix Version,MR Link,Testsheet,"
                            "Specifications,Description"));

    // Hours sit straight after the summary, then sprint, then fix version.
    const QString row = lines.at(1);
    QVERIFY(row.startsWith(QStringLiteral("\"2026-09-08\",\"CADIM-101\",")));
    // Hours are unquoted so Excel treats the column as numbers it can sum.
    QVERIFY2(row.contains(QStringLiteral(",6.0,\"Sprint 12\",\"1.4.0\",")), qPrintable(row));
    QVERIFY(row.contains(QStringLiteral("\"P221997\"")));
    // An embedded quote is doubled, not left to break the field.
    QVERIFY(row.contains(QStringLiteral("Rotate the \"\"signing\"\" certificate")));

    // An empty month is still a valid file with its header.
    const QByteArray empty = jira::buildTimesheetCsv({});
    QVERIFY(empty.startsWith("\xEF\xBB\xBF"));
    QCOMPARE(QString::fromUtf8(empty.mid(3)).count(QLatin1Char('\n')), 1);
}

void TestJiraCore::formatsTrackedTime()
{
    QCOMPARE(jira::formatStopwatch(0), QStringLiteral("0:00:00"));
    QCOMPARE(jira::formatStopwatch(59), QStringLiteral("0:00:59"));
    QCOMPARE(jira::formatStopwatch(3600), QStringLiteral("1:00:00"));
    QCOMPARE(jira::formatStopwatch(3922), QStringLiteral("1:05:22"));
    QCOMPARE(jira::formatStopwatch(-5), QStringLiteral("0:00:00"));

    QCOMPARE(jira::formatTrackedDuration(3922), QStringLiteral("1h 05m"));
    QCOMPARE(jira::formatTrackedDuration(3600), QStringLiteral("1h"));
    QCOMPARE(jira::formatTrackedDuration(900), QStringLiteral("15m"));
    QCOMPARE(jira::formatTrackedDuration(30), QStringLiteral("0m"));
}

void TestJiraCore::runsOneTimerAtATime()
{
    jira::TaskTracker tracker;
    const auto issue = [](const QString &key) {
        jira::Issue i;
        i.key = key;
        i.summary = key + QStringLiteral(" summary");
        return i;
    };
    tracker.merge({issue(QStringLiteral("A-1")), issue(QStringLiteral("A-2"))},
                  {QStringLiteral("A-1"), QStringLiteral("A-2")});
    QCOMPARE(tracker.tasks().size(), 2);
    QVERIFY(tracker.currentIssueKey().isEmpty());

    tracker.start(QStringLiteral("A-1"));
    QCOMPARE(tracker.currentIssueKey(), QStringLiteral("A-1"));
    QVERIFY(tracker.tasks().at(tracker.indexOf(QStringLiteral("A-1"))).isRunning());

    // Starting another must stop the first, not run both.
    tracker.start(QStringLiteral("A-2"));
    QCOMPARE(tracker.currentIssueKey(), QStringLiteral("A-2"));
    QVERIFY(!tracker.tasks().at(tracker.indexOf(QStringLiteral("A-1"))).isRunning());
    QVERIFY(tracker.tasks().at(tracker.indexOf(QStringLiteral("A-2"))).isRunning());

    tracker.stop();
    QVERIFY(!tracker.tasks().at(tracker.indexOf(QStringLiteral("A-2"))).isRunning());

    // Banked time survives a stop and is cleared only on demand.
    jira::TrackedTask manual;
    manual.trackedSeconds = 120;
    QCOMPARE(manual.elapsedSeconds(), qint64(120));
    manual.runningSince = QDateTime::currentDateTime().addSecs(-30);
    QCOMPARE(manual.elapsedSeconds(), qint64(150));
}

void TestJiraCore::dropsTasksNoLongerAssignedToMe()
{
    jira::TaskTracker tracker;
    const auto issue = [](const QString &key) {
        jira::Issue i;
        i.key = key;
        return i;
    };

    tracker.merge({issue(QStringLiteral("A-1")), issue(QStringLiteral("A-2"))},
                  {QStringLiteral("A-1"), QStringLiteral("A-2")});
    QCOMPARE(tracker.tasks().size(), 2);

    // A-2 comes back from Jira assigned to somebody else: done, so it goes.
    tracker.merge({issue(QStringLiteral("A-1")), issue(QStringLiteral("A-2"))},
                  {QStringLiteral("A-1")});
    QCOMPARE(tracker.tasks().size(), 1);
    QCOMPARE(tracker.tasks().first().issueKey, QStringLiteral("A-1"));

    // An issue the search did not return at all is left alone -- it may have
    // moved sprint, or the response may have been partial. Not the same as done.
    tracker.merge({}, {});
    QCOMPARE(tracker.tasks().size(), 1);
}

void TestJiraCore::keepsUnassignedTasksThatHoldTime()
{
    jira::Issue one;
    one.key = QStringLiteral("A-1");

    // Nothing tracked and not running: reassignment deletes it outright.
    {
        jira::TaskTracker idle;
        idle.merge({one}, {QStringLiteral("A-1")});
        idle.merge({one}, {});
        QVERIFY(idle.tasks().isEmpty());
    }

    // Being timed right now: kept and flagged instead, even though a task
    // started this same second has no elapsed whole seconds yet.
    jira::TaskTracker tracker;
    tracker.merge({one}, {QStringLiteral("A-1")});
    tracker.start(QStringLiteral("A-1"));
    QCOMPARE(tracker.tasks().first().elapsedSeconds(), qint64(0));

    tracker.merge({one}, {});   // reassigned away while the clock ran
    QCOMPARE(tracker.tasks().size(), 1);
    QVERIFY(tracker.tasks().first().unassigned);

    // Once it is explicitly removed it is gone for good.
    tracker.remove(QStringLiteral("A-1"));
    QVERIFY(tracker.tasks().isEmpty());
    QVERIFY(tracker.currentIssueKey().isEmpty());
}

void TestJiraCore::recognisesSpecificationFixVersions()
{
    // A P followed by digits is a specification, not a release.
    QVERIFY(jira::isSpecificationVersion(QStringLiteral("P221997")));
    QVERIFY(jira::isSpecificationVersion(QStringLiteral("P1")));
    QVERIFY(jira::isSpecificationVersion(QStringLiteral("  P221997  ")));   // trimmed first

    // Everything that merely starts with a P is not.
    QVERIFY(!jira::isSpecificationVersion(QStringLiteral("Platform 2.0")));
    QVERIFY(!jira::isSpecificationVersion(QStringLiteral("P")));
    QVERIFY(!jira::isSpecificationVersion(QStringLiteral("P221997a")));
    QVERIFY(!jira::isSpecificationVersion(QStringLiteral("XP221997")));
    QVERIFY(!jira::isSpecificationVersion(QStringLiteral("p221997")));      // lower case is not it
    QVERIFY(!jira::isSpecificationVersion(QStringLiteral("1.4.0")));
    QVERIFY(!jira::isSpecificationVersion(QString()));

    QStringList releases;
    QStringList specifications;
    jira::splitFixVersions({QStringLiteral("1.4.0"), QStringLiteral("P221997"),
                            QStringLiteral("Platform 2.0"), QStringLiteral("P8"),
                            QStringLiteral("  ")},
                           &releases, &specifications);
    QCOMPARE(releases, QStringList({QStringLiteral("1.4.0"), QStringLiteral("Platform 2.0")}));
    QCOMPARE(specifications, QStringList({QStringLiteral("P221997"), QStringLiteral("P8")}));

    // Either side may be ignored by the caller.
    QStringList onlySpecs;
    jira::splitFixVersions({QStringLiteral("P1"), QStringLiteral("2.0")}, nullptr, &onlySpecs);
    QCOMPARE(onlySpecs, QStringList({QStringLiteral("P1")}));
    jira::splitFixVersions({QStringLiteral("P1")}, nullptr, nullptr);   // must not crash

    jira::TimesheetEntry entry;
    QVERIFY(!entry.hasSpecifications());
    entry.specifications = QStringLiteral("P221997");
    QVERIFY(entry.hasSpecifications());
}

void TestJiraCore::holidaysOweNothing()
{
    jira::TimesheetRules rules;
    const QDate today(2026, 9, 11);

    // A past day with nothing logged is short; the same day as a holiday is not.
    QCOMPARE(jira::classifyDay(QDate(2026, 9, 10), 0.0, today, rules, false), jira::DayStatus::Short);
    QCOMPARE(jira::classifyDay(QDate(2026, 9, 10), 0.0, today, rules, true), jira::DayStatus::Holiday);

    // Even a part-logged holiday stays a holiday -- the time was volunteered.
    QCOMPARE(jira::classifyDay(QDate(2026, 9, 10), 3.0, today, rules, true), jira::DayStatus::Holiday);

    // A future day is still future; the holiday flag must not override that.
    QCOMPARE(jira::classifyDay(QDate(2026, 9, 25), 0.0, today, rules, true), jira::DayStatus::Future);

    jira::DaySummary holiday;
    holiday.day = QDate(2026, 9, 10);
    holiday.status = jira::DayStatus::Holiday;
    QCOMPARE(holiday.missingHours(rules), 0.0);
    QVERIFY(!holiday.isMissing(rules));
    QVERIFY(holiday.isHoliday());
}

void TestJiraCore::holidaysLeaveTheMonthTotalAlone()
{
    jira::TimesheetRules rules;
    const QDate today(2026, 9, 11);

    jira::TimesheetEntry worked;
    worked.day = QDate(2026, 9, 7);
    worked.issueKey = QStringLiteral("OPS-1");
    worked.hours = 8.0;

    jira::HolidayCalendar holidays;
    QVERIFY(holidays.toggle(QDate(2026, 9, 8)));    // returns the new state
    QVERIFY(holidays.contains(QDate(2026, 9, 8)));
    QVERIFY(!holidays.toggle(QDate(2026, 9, 8)));   // toggles back off
    holidays.add(QDate(2026, 9, 8));
    holidays.add(QDate(2026, 9, 9));

    const QList<jira::DaySummary> days = jira::summariseDays(
            {worked}, QDate(2026, 9, 7), QDate(2026, 9, 11), today, rules, holidays);
    QCOMPARE(days.size(), 5);

    QCOMPARE(days.at(0).status, jira::DayStatus::Complete);   // Mon, worked
    QCOMPARE(days.at(1).status, jira::DayStatus::Holiday);    // Tue
    QCOMPARE(days.at(2).status, jira::DayStatus::Holiday);    // Wed
    QCOMPARE(days.at(3).status, jira::DayStatus::Short);      // Thu
    QCOMPARE(days.at(4).status, jira::DayStatus::Short);      // Fri, today

    // Without the two holidays this month would owe 8+8+8 = 24 h; with them, 16.
    QCOMPARE(jira::totalMissingHours(days, rules), 16.0);
    QCOMPARE(jira::totalLoggedHours(days), 8.0);

    // Weekends were never working days, so a holiday on one is not time off.
    jira::HolidayCalendar weekend;
    weekend.add(QDate(2026, 9, 12));   // Saturday
    weekend.add(QDate(2026, 9, 8));    // Tuesday
    QCOMPARE(weekend.count(), 2);
    QCOMPARE(weekend.countIn(QDate(2026, 9, 1), QDate(2026, 9, 30), rules), 1);
}

void TestJiraCore::persistsHolidaysAcrossLoads()
{
    // Point the application at a scratch data directory so the real one is
    // left alone.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString previous = QCoreApplication::applicationName();
    QCoreApplication::setApplicationName(
            QStringLiteral("JiraDeskTest%1").arg(QDateTime::currentMSecsSinceEpoch()));

    jira::HolidayCalendar saved;
    saved.add(QDate(2026, 12, 24));
    saved.add(QDate(2026, 12, 25));
    saved.add(QDate(2026, 1, 1));
    saved.save();

    jira::HolidayCalendar loaded;
    loaded.load();
    QCOMPARE(loaded.count(), 3);
    QVERIFY(loaded.contains(QDate(2026, 12, 25)));
    QVERIFY(!loaded.contains(QDate(2026, 12, 26)));

    // days() comes back in order, whatever order they went in.
    const QList<QDate> days = loaded.days();
    QCOMPARE(days.first(), QDate(2026, 1, 1));
    QCOMPARE(days.last(), QDate(2026, 12, 25));

    QFile::remove(jira::HolidayCalendar::storagePath());
    QCoreApplication::setApplicationName(previous);
}

void TestJiraCore::validatesWorklogDurations_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<bool>("valid");

    QTest::newRow("hours") << "2h" << true;
    QTest::newRow("hours and minutes") << "1h 30m" << true;
    QTest::newRow("days and hours") << "1d 4h" << true;
    QTest::newRow("weeks") << "2w" << true;
    QTest::newRow("fractional") << "1.5h" << true;
    QTest::newRow("uppercase") << "3H" << true;
    QTest::newRow("no unit") << "90" << false;
    QTest::newRow("wrong unit") << "2s" << false;
    QTest::newRow("empty") << "" << false;
    QTest::newRow("words") << "about two hours" << false;
}

void TestJiraCore::validatesWorklogDurations()
{
    QFETCH(QString, input);
    QFETCH(bool, valid);
    QCOMPARE(jira::isValidDuration(input), valid);
}

QTEST_GUILESS_MAIN(TestJiraCore)
#include "tst_jiracore.moc"
