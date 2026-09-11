#include "data/Database.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStringList>
#include <QVariant>

namespace {

int nextConnectionSerial()
{
    static int serial = 0;
    return ++serial;
}

/*!
 * \brief Schema step 1: the two tables the application stores.
 *
 * Durations are integers (minutes) rather than reals so that a month of
 * quarter hours adds up exactly. Dates are ISO strings, which sort correctly
 * as text and stay readable when the file is opened with any SQLite browser.
 */
QStringList migrationToVersion1()
{
    QStringList statements;

    statements << QStringLiteral(
        "CREATE TABLE IF NOT EXISTS entries ("
        "  id             INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  date           TEXT    NOT NULL,"
        "  start_time     TEXT    NOT NULL DEFAULT '',"
        "  end_time       TEXT    NOT NULL DEFAULT '',"
        "  minutes        INTEGER NOT NULL,"
        "  issue_key      TEXT    NOT NULL DEFAULT '',"
        "  summary        TEXT    NOT NULL DEFAULT '',"
        "  description    TEXT    NOT NULL DEFAULT '',"
        "  project        TEXT    NOT NULL DEFAULT '',"
        "  issue_type     TEXT    NOT NULL DEFAULT '',"
        "  epic           TEXT    NOT NULL DEFAULT '',"
        "  sprint         TEXT    NOT NULL DEFAULT '',"
        "  component      TEXT    NOT NULL DEFAULT '',"
        "  fix_version    TEXT    NOT NULL DEFAULT '',"
        "  branch         TEXT    NOT NULL DEFAULT '',"
        "  merge_request  TEXT    NOT NULL DEFAULT '',"
        "  testsheet_name TEXT    NOT NULL DEFAULT '',"
        "  testsheet_url  TEXT    NOT NULL DEFAULT '',"
        "  issue_url      TEXT    NOT NULL DEFAULT '',"
        "  tags           TEXT    NOT NULL DEFAULT '',"
        "  activity       TEXT    NOT NULL DEFAULT 'development',"
        "  status         TEXT    NOT NULL DEFAULT 'in_progress',"
        "  priority       TEXT    NOT NULL DEFAULT '',"
        "  location       TEXT    NOT NULL DEFAULT '',"
        "  billable       INTEGER NOT NULL DEFAULT 1,"
        "  source         TEXT    NOT NULL DEFAULT 'manual',"
        "  external_id    TEXT    NOT NULL DEFAULT '',"
        "  created_at     TEXT    NOT NULL DEFAULT '',"
        "  updated_at     TEXT    NOT NULL DEFAULT ''"
        ")");

    statements << QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_date ON entries(date)");
    statements << QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_issue ON entries(issue_key)");

    // Partial index: imported rows must stay unique per external worklog, while
    // the many manual rows with an empty external id are left alone.
    statements << QStringLiteral(
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_entries_external "
        "ON entries(external_id) WHERE external_id <> ''");

    statements << QStringLiteral(
        "CREATE TABLE IF NOT EXISTS day_meta ("
        "  date            TEXT PRIMARY KEY,"
        "  day_type        TEXT    NOT NULL DEFAULT 'workday',"
        "  target_minutes  INTEGER NOT NULL DEFAULT -1,"
        "  note            TEXT    NOT NULL DEFAULT ''"
        ")");

    return statements;
}

} // namespace

Database::Database()
    : m_connectionName(QStringLiteral("workcalendar_%1").arg(nextConnectionSerial()))
{
}

Database::~Database()
{
    close();
}

QString Database::defaultPath()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(directory).filePath(QStringLiteral("worklog.sqlite"));
}

int Database::expectedSchemaVersion()
{
    return 1;
}

bool Database::open(const QString &path, QString *error)
{
    close();

    if (path != QStringLiteral(":memory:")) {
        const QString directory = QFileInfo(path).absolutePath();
        if (!QDir().mkpath(directory)) {
            if (error)
                *error = QStringLiteral("Cannot create the folder %1.").arg(directory);
            return false;
        }
    }

    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    database.setDatabaseName(path);

    if (!database.open()) {
        if (error)
            *error = database.lastError().text();
        QSqlDatabase::removeDatabase(m_connectionName);
        return false;
    }

    m_open = true;

    {
        // Foreign keys are off by default in SQLite, and WAL keeps the
        // application responsive while a long export reads the same file.
        //
        // A PRAGMA answers with a row, and an unread row leaves its statement
        // active - which makes the very next commit fail with "SQL statements
        // in progress". Hence the explicit next()/finish() and the tight scope:
        // the query must be finished and gone before the migration starts.
        QSqlQuery pragma(database);

        pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
        pragma.next();
        pragma.finish();

        if (path != QStringLiteral(":memory:")) {
            pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
            pragma.next();
            pragma.finish();
        }
    }

    if (!applyMigrations(error)) {
        close();
        return false;
    }

    return true;
}

void Database::close()
{
    if (!m_open)
        return;

    {
        QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
        if (database.isValid() && database.isOpen())
            database.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
    m_open = false;
}

bool Database::isOpen() const
{
    return m_open;
}

QSqlDatabase Database::connection() const
{
    return QSqlDatabase::database(m_connectionName, false);
}

int Database::schemaVersion() const
{
    QSqlQuery query(connection());
    if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next())
        return 0;
    return query.value(0).toInt();
}

void Database::setSchemaVersion(int version)
{
    QSqlQuery query(connection());
    query.exec(QStringLiteral("PRAGMA user_version = %1").arg(version));
}

bool Database::runStatements(const QStringList &statements, QString *error)
{
    QSqlDatabase database = connection();
    for (const QString &statement : statements) {
        QSqlQuery query(database);
        if (!query.exec(statement)) {
            if (error) {
                *error = QStringLiteral("%1\n\nwhile running:\n%2")
                             .arg(query.lastError().text(), statement);
            }
            return false;
        }
    }
    return true;
}

bool Database::applyMigrations(QString *error)
{
    int version = schemaVersion();
    if (version >= expectedSchemaVersion())
        return true;

    QSqlDatabase database = connection();
    database.transaction();

    if (version < 1) {
        if (!runStatements(migrationToVersion1(), error)) {
            database.rollback();
            return false;
        }
        version = 1;
    }

    // Later schema steps go here, each guarded by "if (version < n)".

    if (!database.commit()) {
        if (error)
            *error = database.lastError().text();
        return false;
    }

    setSchemaVersion(version);
    return true;
}
