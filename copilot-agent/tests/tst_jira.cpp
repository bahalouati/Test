// Jira: keys, credentials, ticket -> task text, and the REST client against a
// small fake Jira served from this test.
#include "jiraconnection.h"
#include "jiraissue.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

#include <memory>

namespace {

// Answers one canned response per request path and remembers what it got.
class FakeJira : public QObject
{
public:
    struct Seen { QByteArray method, path, auth, body; };
    QVector<Seen> seen;
    QHash<QByteArray, QPair<int, QByteArray>> routes;   // "METHOD path" -> (status, body)

    FakeJira()
    {
        server.listen(QHostAddress::LocalHost);
        QObject::connect(&server, &QTcpServer::newConnection, [this] {
            while (QTcpSocket *s = server.nextPendingConnection()) {
                auto buf = std::make_shared<QByteArray>();
                QObject::connect(s, &QTcpSocket::readyRead, s, [this, s, buf] {
                    *buf += s->readAll();
                    const int headerEnd = buf->indexOf("\r\n\r\n");
                    if (headerEnd < 0)
                        return;
                    const QList<QByteArray> lines = buf->left(headerEnd).split('\n');
                    int length = 0;
                    QByteArray auth;
                    for (const QByteArray &l : lines) {
                        const QByteArray t = l.trimmed();
                        if (t.toLower().startsWith("content-length:"))
                            length = t.mid(15).trimmed().toInt();
                        if (t.toLower().startsWith("authorization:"))
                            auth = t.mid(14).trimmed();
                    }
                    if (buf->size() < headerEnd + 4 + length)
                        return;
                    const QList<QByteArray> first = lines.first().trimmed().split(' ');
                    Seen r{first.value(0), first.value(1), auth, buf->mid(headerEnd + 4, length)};
                    seen.append(r);
                    QByteArray route = r.method + ' ' + r.path;
                    const auto answer = routes.value(route, qMakePair(404, QByteArray("{\"errorMessages\":[\"Issue does not exist\"]}")));
                    const QByteArray reply = "HTTP/1.1 " + QByteArray::number(answer.first) + " X\r\n"
                                             "Content-Type: application/json\r\nContent-Length: "
                                           + QByteArray::number(answer.second.size()) + "\r\nConnection: close\r\n\r\n"
                                           + answer.second;
                    s->write(reply);
                    s->disconnectFromHost();
                });
            }
        });
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1/jira").arg(server.serverPort()); }
    QTcpServer server;
};

QJsonObject sampleIssue()
{
    QFile f(QStringLiteral(FIXTURES_DIR "/jira-issue.json"));
    if (!f.open(QIODevice::ReadOnly))
        qFatal("missing jira-issue.json");
    return QJsonDocument::fromJson(f.readAll()).object();
}

} // namespace

class TestJira : public QObject
{
    Q_OBJECT
private slots:
    void keys()
    {
        QCOMPARE(JiraConn::issueKeyFrom("PROJ-123"), QStringLiteral("PROJ-123"));
        QCOMPARE(JiraConn::issueKeyFrom(" proj-7 "), QStringLiteral("PROJ-7"));
        QCOMPARE(JiraConn::issueKeyFrom("https://jira.example.com/browse/AB_C-99?focused=1"), QStringLiteral("AB_C-99"));
        QVERIFY(JiraConn::issueKeyFrom("123-45").isEmpty());
        QVERIFY(JiraConn::issueKeyFrom("hello").isEmpty());
    }

    void credentials()
    {
        QCOMPARE(JiraConn::normalizeBaseUrl(" jira.example.com/rest/api/2/ "), QStringLiteral("https://jira.example.com"));
        QCOMPARE(JiraConn::normalizeBaseUrl("https://x.com/jira/"), QStringLiteral("https://x.com/jira"));
        QCOMPARE(JiraConn::restEndpoint("https://x.com/jira", "issue/A-1").toString(),
                 QStringLiteral("https://x.com/jira/rest/api/2/issue/A-1"));
        JiraConn::Credentials c;
        c.baseUrl = "x.com";
        c.token = "tok";
        QCOMPARE(c.authorizationHeader(), QByteArray("Bearer tok"));
        QVERIFY(c.isComplete());
        c.mode = JiraConn::AuthMode::Basic;
        QVERIFY(!c.isComplete());                       // Cloud needs the e-mail
        c.username = "me@x.com";
        QCOMPARE(c.authorizationHeader(), "Basic " + QByteArray("me@x.com:tok").toBase64());
    }

    void issueToTask()
    {
        const QJsonObject issue = sampleIssue();
        QCOMPARE(JiraIssue::briefOf(issue).line(), QStringLiteral("PROJ-42 · Bug · In Progress · Timer shows the wrong time after midnight"));

        const QVector<JiraIssue::Attachment> atts = JiraIssue::readableAttachments(issue);
        QCOMPARE(atts.size(), 1);                        // not the image, not the 95 MB log
        QCOMPARE(atts.first().name, QStringLiteral("spec.txt"));

        const QString t = JiraIssue::toTask(issue, {{"spec.txt", "The clock must use local time."}}, "Keep the UI as is.");
        QVERIFY2(t.startsWith("Jira ticket PROJ-42: Timer shows the wrong time after midnight\n"
                              "type Bug, status In Progress, priority High, labels clock, ui, components Display, fix versions 2.4."), qPrintable(t));
        QVERIFY(t.contains("Description:\nAfter 00:00 the timer keeps the old date."));
        QVERIFY(t.contains("Acceptance Criteria: Shows today's date within 1 s of midnight."));
        QVERIFY(t.contains("Steps to Reproduce: 1. Run until 23:59:59\n2. Wait"));
        QVERIFY(t.contains("Severity: Major"));
        QVERIFY(!t.contains("Sprint"));                  // noise
        QVERIFY(t.contains("- Ann (2026-11-20): First comment\n- Bob (2026-11-21): Second comment"));
        QVERIFY(t.contains("- spec.txt:\nThe clock must use local time."));
        QVERIFY(t.contains("- screen.png (not included)"));
        QVERIFY(t.endsWith("My notes on this ticket:\nKeep the UI as is."));

        // A huge description is cut, and the whole stays near the budget.
        QJsonObject big = issue;
        QJsonObject fields = big.value("fields").toObject();
        fields.insert("description", QString(20000, QLatin1Char('x')));
        big.insert("fields", fields);
        const QString b = JiraIssue::toTask(big, {}, QString(), 8000);
        QVERIFY2(b.size() < 9000, qPrintable(QString::number(b.size())));
        QVERIFY(b.contains("more characters not shown"));
    }

    void wrapUpComment()
    {
        QCOMPARE(JiraIssue::wrapUpComment("Fixed it.", {"src/a.cpp", "src/b.h"}, "PROJ-42-fix"),
                 QStringLiteral("Fixed it.\n\nFiles changed:\n* src/a.cpp\n* src/b.h\n\nBranch: PROJ-42-fix"));
        QCOMPARE(JiraIssue::wrapUpComment(QString(), {}, QString()), QStringLiteral("Done."));
    }

    void clientAgainstFakeJira()
    {
        FakeJira fake;
        fake.routes.insert("GET /jira/rest/api/2/issue/PROJ-42?fields=*all&expand=names",
                           {200, QJsonDocument(sampleIssue()).toJson()});
        fake.routes.insert("POST /jira/rest/api/2/issue/PROJ-42/comment", {201, "{\"id\":\"1\"}"});
        fake.routes.insert("GET /jira/rest/api/2/issue/PROJ-42/transitions",
                           {200, "{\"transitions\":[{\"id\":\"31\",\"name\":\"Review\",\"to\":{\"name\":\"In Review\"}}]}"});
        fake.routes.insert("POST /jira/rest/api/2/issue/PROJ-42/transitions", {204, ""});
        fake.routes.insert("GET /jira/secure/attachment/1/spec.txt", {200, "attachment text"});

        JiraConn::Client client;
        JiraConn::Credentials c;
        c.baseUrl = fake.base();
        c.token = "secret";
        client.setCredentials(c);

        auto wait = [](const std::shared_ptr<bool> &done) { return QTest::qWaitFor([&] { return *done; }, 10000); };

        auto done = std::make_shared<bool>(false);
        QJsonObject issue;
        client.fetchIssue("PROJ-42", [&, done](const QJsonValue &v, const QString &e) { QVERIFY2(e.isEmpty(), qPrintable(e)); issue = v.toObject(); *done = true; });
        QVERIFY(wait(done));
        QCOMPARE(issue.value("key").toString(), QStringLiteral("PROJ-42"));
        QCOMPARE(fake.seen.last().auth, QByteArray("Bearer secret"));

        done = std::make_shared<bool>(false);
        client.addComment("PROJ-42", "All \"done\"", [&, done](const QJsonValue &, const QString &e) { QVERIFY(e.isEmpty()); *done = true; });
        QVERIFY(wait(done));
        QCOMPARE(QJsonDocument::fromJson(fake.seen.last().body).object().value("body").toString(), QStringLiteral("All \"done\""));

        done = std::make_shared<bool>(false);
        QVector<JiraConn::Transition> ts;
        client.fetchTransitions("PROJ-42", [&, done](const QVector<JiraConn::Transition> &t, const QString &) { ts = t; *done = true; });
        QVERIFY(wait(done));
        QCOMPARE(ts.size(), 1);
        QCOMPARE(ts.first().toStatus, QStringLiteral("In Review"));

        done = std::make_shared<bool>(false);
        client.applyTransition("PROJ-42", "31", [&, done](const QJsonValue &, const QString &e) { QVERIFY(e.isEmpty()); *done = true; });
        QVERIFY(wait(done));
        QCOMPARE(QJsonDocument::fromJson(fake.seen.last().body).object().value("transition").toObject().value("id").toString(),
                 QStringLiteral("31"));

        done = std::make_shared<bool>(false);
        QByteArray bytes;
        client.download(QUrl(fake.base().replace("/jira", "") + "/jira/secure/attachment/1/spec.txt"), 1024,
                        [&, done](const QByteArray &b, const QString &e) { QVERIFY(e.isEmpty()); bytes = b; *done = true; });
        QVERIFY(wait(done));
        QCOMPARE(bytes, QByteArray("attachment text"));

        done = std::make_shared<bool>(false);
        QString error;
        client.fetchIssue("NOPE-1", [&, done](const QJsonValue &, const QString &e) { error = e; *done = true; });
        QVERIFY(wait(done));
        QVERIFY2(error.contains("404") && error.contains("Issue does not exist"), qPrintable(error));
    }

    void unconfiguredClientSaysSo()
    {
        JiraConn::Client client;
        auto done = std::make_shared<bool>(false);
        QString error;
        client.fetchMyself([&, done](const QJsonValue &, const QString &e) { error = e; *done = true; });
        QVERIFY(QTest::qWaitFor([&] { return *done; }, 5000));
        QVERIFY(error.contains("Jira settings"));
    }
};

QTEST_GUILESS_MAIN(TestJira)
#include "tst_jira.moc"
