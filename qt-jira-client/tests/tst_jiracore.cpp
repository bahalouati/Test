#include "core/credentials.h"
#include "core/jiraclient.h"
#include "core/jiratypes.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
