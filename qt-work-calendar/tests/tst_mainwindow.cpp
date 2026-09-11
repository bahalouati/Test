#include "data/Database.h"
#include "data/WorkEntryRepository.h"
#include "ui/MainWindow.h"

#include <QComboBox>
#include <QDateEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QtTest>

/*!
 * \brief Builds a real window over a real database and drives it.
 *
 * This is a smoke test rather than a test of the interface's looks: it proves
 * that the .ui files load, that every widget the code asks for by name exists,
 * that the models are wired up, and that navigating, filtering and reporting
 * do not fall over. Those are exactly the mistakes that only show up when the
 * application is started.
 *
 * The widgets are reached through findChild() and their object names, so the
 * test reads the window the same way Qt Designer names it.
 */
class TestMainWindow : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void buildsEveryNamedWidget();
    void showsTheMonthOfTheStoredEntries();
    void navigatesBetweenMonths();
    void fillsTheEntryTableAndFiltersIt();
    void fillsTheReportTab();

private:
    /*! Writes a few entries into \a path, so the window has something to show. */
    static void seedDatabase(const QString &path);

    /*! Looks up a widget by object name, failing the test when it is missing. */
    template <typename T>
    T *widget(const char *name) const
    {
        T *found = m_window->findChild<T *>(QString::fromLatin1(name));
        return found;
    }

    QTemporaryDir *m_directory = nullptr;
    MainWindow *m_window = nullptr;
};

void TestMainWindow::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("WorkCalendarTests"));
    QCoreApplication::setApplicationName(QStringLiteral("MainWindowTest"));
}

void TestMainWindow::seedDatabase(const QString &path)
{
    Database database;
    QString error;
    QVERIFY2(database.open(path, &error), qPrintable(error));

    WorkEntryRepository repository(database);

    // Three entries in one month: two on the same day, one on another, with
    // different projects and activities so the filters have something to do.
    WorkEntry first;
    first.date = QDate(2026, 9, 7);
    first.minutes = 300;
    first.issueKey = QStringLiteral("PLAT-1");
    first.summary = QStringLiteral("Write the importer");
    first.activity = Activity::Development;
    first.sprint = QStringLiteral("Sprint 37");
    QVERIFY2(repository.add(first, &error), qPrintable(error));

    WorkEntry second;
    second.date = QDate(2026, 9, 7);
    second.minutes = 180;
    second.issueKey = QStringLiteral("PLAT-1");
    second.summary = QStringLiteral("Review the importer");
    second.activity = Activity::CodeReview;
    QVERIFY2(repository.add(second, &error), qPrintable(error));

    WorkEntry third;
    third.date = QDate(2026, 9, 8);
    third.minutes = 120;
    third.issueKey = QStringLiteral("SUP-9");
    third.summary = QStringLiteral("Customer call");
    third.activity = Activity::Support;
    third.billable = false;
    QVERIFY2(repository.add(third, &error), qPrintable(error));
}

void TestMainWindow::init()
{
    m_directory = new QTemporaryDir;
    QVERIFY(m_directory->isValid());

    const QString path = m_directory->filePath(QStringLiteral("worklog.sqlite"));
    seedDatabase(path);

    m_window = new MainWindow;
    QVERIFY(m_window->initialise(path));
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window));
}

void TestMainWindow::cleanup()
{
    delete m_window;
    m_window = nullptr;
    delete m_directory;
    m_directory = nullptr;
}

void TestMainWindow::buildsEveryNamedWidget()
{
    // The names the code looks up in MainWindow.cpp must all exist; a typo in
    // the .ui file would otherwise only be found at run time.
    QVERIFY(widget<QTabWidget>("tabWidget"));
    QVERIFY(widget<QComboBox>("comboMonth"));
    QVERIFY(widget<QSpinBox>("spinYear"));
    QVERIFY(widget<QTableView>("tableEntries"));
    QVERIFY(widget<QTableView>("tableDayEntries"));
    QVERIFY(widget<QTableView>("tableBreakdown"));
    QVERIFY(widget<QLabel>("labelMonthSummary"));
    QVERIFY(widget<QLabel>("labelSelectedDate"));
    QVERIFY(widget<QLabel>("labelTotalBalance"));

    // The models are attached, which is what makes the views show anything.
    QVERIFY(widget<QTableView>("tableEntries")->model() != nullptr);
    QVERIFY(widget<QTableView>("tableDayEntries")->model() != nullptr);
    QVERIFY(widget<QTableView>("tableBreakdown")->model() != nullptr);
}

void TestMainWindow::showsTheMonthOfTheStoredEntries()
{
    QComboBox *month = widget<QComboBox>("comboMonth");
    QSpinBox *year = widget<QSpinBox>("spinYear");
    QVERIFY(month && year);

    // The window opens on the current month; navigate to the seeded one.
    month->setCurrentIndex(8);   // September
    year->setValue(2026);

    QTableView *entries = widget<QTableView>("tableEntries");
    QCOMPARE(entries->model()->rowCount(), 3);

    // The month summary mentions the eight hours booked on 7 September and the
    // two on the 8th, ten in total.
    const QString summary = widget<QLabel>("labelMonthSummary")->text();
    QVERIFY2(summary.contains(QStringLiteral("10h")), qPrintable(summary));
}

void TestMainWindow::navigatesBetweenMonths()
{
    QComboBox *month = widget<QComboBox>("comboMonth");
    QSpinBox *year = widget<QSpinBox>("spinYear");

    month->setCurrentIndex(0);   // January
    year->setValue(2026);

    QVERIFY(QMetaObject::invokeMethod(m_window, "onNextMonth"));
    QCOMPARE(month->currentIndex(), 1);
    QCOMPARE(year->value(), 2026);

    QVERIFY(QMetaObject::invokeMethod(m_window, "onPreviousMonth"));
    QVERIFY(QMetaObject::invokeMethod(m_window, "onPreviousMonth"));
    QCOMPARE(month->currentIndex(), 11);   // December
    QCOMPARE(year->value(), 2025);         // of the previous year

    QVERIFY(QMetaObject::invokeMethod(m_window, "onGoToToday"));
    QCOMPARE(month->currentIndex(), QDate::currentDate().month() - 1);
    QCOMPARE(year->value(), QDate::currentDate().year());

    // Reloading everything must leave the window standing.
    QVERIFY(QMetaObject::invokeMethod(m_window, "onRefresh"));
}

void TestMainWindow::fillsTheEntryTableAndFiltersIt()
{
    QTabWidget *tabs = widget<QTabWidget>("tabWidget");
    tabs->setCurrentIndex(1);   // the Entries tab

    QTableView *table = widget<QTableView>("tableEntries");
    QCOMPARE(table->model()->rowCount(), 3);

    QLineEdit *search = widget<QLineEdit>("lineSearch");
    QVERIFY(search);
    search->setText(QStringLiteral("importer"));
    QCOMPARE(table->model()->rowCount(), 2);

    search->setText(QStringLiteral("Customer"));
    QCOMPARE(table->model()->rowCount(), 1);

    // The search covers fields that are not shown as columns.
    search->setText(QStringLiteral("Sprint 37"));
    QCOMPARE(table->model()->rowCount(), 1);

    QComboBox *billable = widget<QComboBox>("comboFilterBillable");
    QVERIFY(billable);
    search->clear();
    billable->setCurrentIndex(2);   // not billable
    QCOMPARE(table->model()->rowCount(), 1);

    QVERIFY(QMetaObject::invokeMethod(m_window, "onClearFilters"));
    QCOMPARE(table->model()->rowCount(), 3);
}

void TestMainWindow::fillsTheReportTab()
{
    QTabWidget *tabs = widget<QTabWidget>("tabWidget");
    tabs->setCurrentIndex(2);   // the Reports tab

    QComboBox *period = widget<QComboBox>("comboReportPeriod");
    QVERIFY(period);
    period->setCurrentIndex(period->count() - 1);   // Custom

    QDateEdit *from = widget<QDateEdit>("dateReportFrom");
    QDateEdit *to = widget<QDateEdit>("dateReportTo");
    QVERIFY(from && to);
    from->setDate(QDate(2026, 9, 1));
    to->setDate(QDate(2026, 9, 30));

    QVERIFY(QMetaObject::invokeMethod(m_window, "onRefreshReport"));

    QCOMPARE(widget<QLabel>("labelTotalLogged")->text(), QStringLiteral("10h"));
    QCOMPARE(widget<QLabel>("labelTotalEntries")->text(), QStringLiteral("3"));

    // Grouping by project splits the ten hours into eight and two.
    QTableView *breakdown = widget<QTableView>("tableBreakdown");
    QComboBox *groupBy = widget<QComboBox>("comboGroupBy");
    QVERIFY(groupBy);
    groupBy->setCurrentIndex(0);   // Project
    QCOMPARE(breakdown->model()->rowCount(), 2);
    QCOMPARE(breakdown->model()->index(0, 0).data().toString(), QStringLiteral("PLAT"));
    QCOMPARE(breakdown->model()->index(0, 1).data().toString(), QStringLiteral("8h"));

    QVERIFY(!widget<QPlainTextEdit>("textRecap")->toPlainText().isEmpty());
}

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
