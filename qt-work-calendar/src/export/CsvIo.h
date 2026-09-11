#ifndef WORKCALENDAR_CSVIO_H
#define WORKCALENDAR_CSVIO_H

#include "core/Duration.h"
#include "core/WorkEntry.h"

#include <QString>
#include <QStringList>
#include <QVector>

/*!
 * \file CsvIo.h
 * \brief Reads and writes work entries as CSV.
 *
 * The format follows RFC 4180: fields are separated by a comma, a field
 * containing a comma, a quote or a newline is wrapped in double quotes, and a
 * quote inside such a field is doubled. Files are written as UTF-8 with a byte
 * order mark, which is what spreadsheet applications expect.
 */
namespace CsvIo {

/*! What an import did, so the user gets a report rather than a silent result. */
struct ImportResult
{
    int rowsRead = 0;
    int imported = 0;
    int skipped = 0;
    QStringList problems; //!< one line per rejected row, with its row number

    bool hasProblems() const { return !problems.isEmpty(); }
};

/*!
 * \brief Writes \a entries to \a path with the given \a columns.
 * \param[out] error  why it failed, when it does
 */
bool writeEntries(const QString &path,
                  const QVector<WorkEntry> &entries,
                  const QVector<WorkEntry::Field> &columns,
                  QString *error = nullptr);

/*!
 * \brief Reads entries from \a path.
 *
 * Columns are matched by their header text against both the displayed header
 * ("Fix version") and the stored token ("fix_version"), so a file exported by
 * this application imports unchanged and a hand-made file only needs sensible
 * headers. Unknown columns are ignored; rows that cannot make a valid entry
 * are reported in \a result rather than aborting the import.
 *
 * The id, external id and source columns are never taken from the file: the
 * first two identify rows in whatever database produced it, and every imported
 * entry is marked as having come from a CSV.
 */
bool readEntries(const QString &path,
                 QVector<WorkEntry> *entries,
                 ImportResult *result,
                 QString *error = nullptr);

/*! Splits one CSV line into fields. Exposed for the tests. */
QStringList parseLine(const QString &line);

/*! Quotes \a value if it needs it. Exposed for the tests. */
QString quote(const QString &value);

} // namespace CsvIo

#endif // WORKCALENDAR_CSVIO_H
