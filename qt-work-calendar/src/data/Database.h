#ifndef WORKCALENDAR_DATABASE_H
#define WORKCALENDAR_DATABASE_H

#include <QSqlDatabase>
#include <QString>

/*!
 * \brief Owns the SQLite connection and keeps its schema up to date.
 *
 * The schema version is kept in SQLite's own \c user_version pragma and raised
 * by applyMigrations(). Adding a column later means appending one more step
 * there; existing files are upgraded in place on the next start, so a user
 * never loses a database to a version bump.
 *
 * Each instance owns a uniquely named connection, which lets the tests open
 * several independent in-memory databases in one process.
 */
class Database
{
public:
    Database();
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    /*! Where the live database is kept, under the user's application data. */
    static QString defaultPath();

    /*!
     * \brief Opens \a path, creating the file and the schema when needed.
     *
     * Pass ":memory:" for a throwaway database.
     * \param[out] error  a message suitable for a dialog, when opening fails
     */
    bool open(const QString &path, QString *error = nullptr);

    void close();

    bool isOpen() const;

    /*! The connection the repositories run their queries on. */
    QSqlDatabase connection() const;

    /*! The schema version currently stored in the file. */
    int schemaVersion() const;

    /*! The schema version this build expects. */
    static int expectedSchemaVersion();

private:
    bool applyMigrations(QString *error);
    bool runStatements(const QStringList &statements, QString *error);
    void setSchemaVersion(int version);

    QString m_connectionName;
    bool m_open = false;
};

#endif // WORKCALENDAR_DATABASE_H
