#include "core/Duration.h"

#include <QtTest>

/*!
 * \brief Checks the duration parser against the shapes people actually type.
 *
 * This is the one piece of the application everybody touches several times a
 * day, so every accepted spelling is pinned down here.
 */
class TestDuration : public QObject
{
    Q_OBJECT

private slots:
    void parsesHoursAndMinutes_data();
    void parsesHoursAndMinutes();
    void rejectsNonsense_data();
    void rejectsNonsense();
    void formatsReadably_data();
    void formatsReadably();
    void formatsSignedBalances();
    void roundsUpToIncrement_data();
    void roundsUpToIncrement();
    void convertsToHours();
};

void TestDuration::parsesHoursAndMinutes_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("expectedMinutes");

    QTest::newRow("bare hours")        << "7"        << 420;
    QTest::newRow("decimal hours")     << "7.5"      << 450;
    QTest::newRow("comma decimal")     << "7,5"      << 450;
    QTest::newRow("hours suffix")      << "7h"       << 420;
    QTest::newRow("hours and minutes") << "7h30"     << 450;
    QTest::newRow("spaced")            << "7h 30m"   << 450;
    QTest::newRow("clock")             << "7:30"     << 450;
    QTest::newRow("minutes only")      << "90m"      << 90;
    QTest::newRow("minutes word")      << "90 min"   << 90;
    QTest::newRow("quarter hour")      << "0.25"     << 15;
    QTest::newRow("upper case")        << "2H15"     << 135;
    QTest::newRow("surrounding space") << "  3h  "   << 180;
    QTest::newRow("decimal hours plus minutes") << "1.5h" << 90;
}

void TestDuration::parsesHoursAndMinutes()
{
    QFETCH(QString, text);
    QFETCH(int, expectedMinutes);

    bool ok = false;
    const int minutes = Duration::parseMinutes(text, &ok);

    QVERIFY2(ok, qPrintable(QStringLiteral("'%1' was not understood").arg(text)));
    QCOMPARE(minutes, expectedMinutes);
}

void TestDuration::rejectsNonsense_data()
{
    QTest::addColumn<QString>("text");

    QTest::newRow("empty")      << "";
    QTest::newRow("letters")    << "soon";
    QTest::newRow("negative")   << "-2h";
    QTest::newRow("two units")  << "2h 3h";
    QTest::newRow("stray text") << "about 2h";
}

void TestDuration::rejectsNonsense()
{
    QFETCH(QString, text);

    bool ok = true;
    const int minutes = Duration::parseMinutes(text, &ok);

    QVERIFY(!ok);
    QCOMPARE(minutes, 0);
}

void TestDuration::formatsReadably_data()
{
    QTest::addColumn<int>("minutes");
    QTest::addColumn<QString>("expected");

    QTest::newRow("whole hours")   << 480 << "8h";
    QTest::newRow("hours minutes") << 450 << "7h 30m";
    QTest::newRow("minutes only")  << 45  << "45m";
    QTest::newRow("zero")          << 0   << "0m";
}

void TestDuration::formatsReadably()
{
    QFETCH(int, minutes);
    QFETCH(QString, expected);

    QCOMPARE(Duration::format(minutes), expected);
}

void TestDuration::formatsSignedBalances()
{
    QCOMPARE(Duration::formatSigned(90), QStringLiteral("+1h 30m"));
    QCOMPARE(Duration::formatSigned(-90), QStringLiteral("-1h 30m"));
    QCOMPARE(Duration::formatSigned(0), QStringLiteral("0"));

    QCOMPARE(Duration::format(450, Duration::Format::Decimal), QStringLiteral("7.50 h"));
    QCOMPARE(Duration::format(450, Duration::Format::Clock), QStringLiteral("7:30"));
}

void TestDuration::roundsUpToIncrement_data()
{
    QTest::addColumn<int>("minutes");
    QTest::addColumn<int>("increment");
    QTest::addColumn<int>("expected");

    QTest::newRow("already aligned")  << 30 << 15 << 30;
    QTest::newRow("rounds up")        << 31 << 15 << 45;
    QTest::newRow("one minute up")    << 1  << 15 << 15;
    QTest::newRow("no rounding")      << 37 << 0  << 37;
    QTest::newRow("increment of one") << 37 << 1  << 37;
    QTest::newRow("zero stays zero")  << 0  << 15 << 0;
}

void TestDuration::roundsUpToIncrement()
{
    QFETCH(int, minutes);
    QFETCH(int, increment);
    QFETCH(int, expected);

    QCOMPARE(Duration::roundUpTo(minutes, increment), expected);
}

void TestDuration::convertsToHours()
{
    QCOMPARE(Duration::toHours(90), 1.5);
    QCOMPARE(Duration::fromHours(1.5), 90);
    QCOMPARE(Duration::fromHours(0.25), 15);

    // Minutes in, minutes out: the round trip must not drift.
    for (int minutes = 0; minutes < 24 * 60; ++minutes)
        QCOMPARE(Duration::fromHours(Duration::toHours(minutes)), minutes);
}

QTEST_GUILESS_MAIN(TestDuration)
#include "tst_duration.moc"
