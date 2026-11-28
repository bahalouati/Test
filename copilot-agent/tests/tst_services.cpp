#include "copilotservice.h"

#include <QtTest>

using namespace CopilotService;

class TestServices : public QObject
{
    Q_OBJECT
private slots:
    void kinds()
    {
        QCOMPARE(kindOfUrl("https://m365.cloud.microsoft/chat"), Work);
        QCOMPARE(kindOfUrl("copilot.cloud.microsoft/chat/conversation/1"), Work);
        QCOMPARE(kindOfUrl("https://copilot.com/chat"), Personal);
        QCOMPARE(kindOfUrl("https://copilot.microsoft.com/"), Personal);
        QCOMPARE(kindOfUrl("https://example.com/chat"), Custom);
        QCOMPARE(kindOfUrl(defaultUrl(Work)), Work);
        QCOMPARE(kindOfUrl(defaultUrl(Personal)), Personal);
    }

    void neverMixesWorkAndPersonal()
    {
        // Redirects within one service still find the tab...
        QVERIFY(sameService("copilot.cloud.microsoft", "m365.cloud.microsoft"));
        QVERIFY(sameService("copilot.microsoft.com", "copilot.com"));
        // ...but a work tab is never taken for personal Copilot, or the reverse.
        QVERIFY(!sameService("copilot.microsoft.com", "m365.cloud.microsoft"));
        QVERIFY(!sameService("copilot.cloud.microsoft", "copilot.com"));
        QVERIFY(!sameService("example.com", "other.example.com"));
        QVERIFY(sameService("example.com", "example.com"));
    }

    void signInAndProfiles()
    {
        QVERIFY(isSignInHost("login.live.com", Personal));
        QVERIFY(!isSignInHost("login.live.com", Work));
        QVERIFY(isSignInHost("login.microsoftonline.com", Work));
        QVERIFY(profileFolder(Work) != profileFolder(Personal));
        QCOMPARE(profileFolder(Work), QStringLiteral("edge-profile"));   // existing sign-ins keep working
    }
};

QTEST_GUILESS_MAIN(TestServices)
#include "tst_services.moc"
