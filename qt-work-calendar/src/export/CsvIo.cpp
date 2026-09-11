#include "export/CsvIo.h"

#include <QFile>
#include <QTextStream>

namespace {

/*!
 * \brief Splits \a text into records, honouring quoted fields.
 *
 * A newline inside a quoted field belongs to that field, so the file cannot
 * simply be split on '\n' first. This walks the text once and emits complete
 * records.
 */
QVector<QStringList> parseCsv(const QString &text)
{
    QVector<QStringList> records;
    QStringList currentRecord;
    QString currentField;
    bool inQuotes = false;

    int index = 0;
    while (index < text.size()) {
        const QChar character = text.at(index);

        if (inQuotes) {
            if (character == QLatin1Char('"')) {
                const bool isEscapedQuote = index + 1 < text.size()
                                            && text.at(index + 1) == QLatin1Char('"');
                if (isEscapedQuote) {
                    currentField += QLatin1Char('"');
                    index += 2;
                    continue;
                }
                inQuotes = false;
                ++index;
                continue;
            }
            currentField += character;
            ++index;
            continue;
        }

        if (character == QLatin1Char('"')) {
            inQuotes = true;
            ++index;
            continue;
        }

        if (character == QLatin1Char(',')) {
            currentRecord.append(currentField);
            currentField.clear();
            ++index;
            continue;
        }

        if (character == QLatin1Char('\n') || character == QLatin1Char('\r')) {
            // Accept both line endings; a CRLF counts as one break.
            if (character == QLatin1Char('\r') && index + 1 < text.size()
                && text.at(index + 1) == QLatin1Char('\n')) {
                ++index;
            }
            currentRecord.append(currentField);
            currentField.clear();
            records.append(currentRecord);
            currentRecord.clear();
            ++index;
            continue;
        }

        currentField += character;
        ++index;
    }

    if (!currentField.isEmpty() || !currentRecord.isEmpty()) {
        currentRecord.append(currentField);
        records.append(currentRecord);
    }

    return records;
}

} // namespace

QString CsvIo::quote(const QString &value)
{
    const bool needsQuotes = value.contains(QLatin1Char(','))
                             || value.contains(QLatin1Char('"'))
                             || value.contains(QLatin1Char('\n'))
                             || value.contains(QLatin1Char('\r'));
    if (!needsQuotes)
        return value;

    QString quoted = value;
    quoted.replace(QLatin1String("\""), QLatin1String("\"\""));
    return QLatin1Char('"') + quoted + QLatin1Char('"');
}

QStringList CsvIo::parseLine(const QString &line)
{
    const QVector<QStringList> records = parseCsv(line);
    return records.isEmpty() ? QStringList() : records.first();
}

bool CsvIo::writeEntries(const QString &path,
                         const QVector<WorkEntry> &entries,
                         const QVector<WorkEntry::Field> &columns,
                         QString *error)
{
    const QVector<WorkEntry::Field> usedColumns =
        columns.isEmpty() ? WorkEntry::allFields() : columns;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream.setGenerateByteOrderMark(true);

    QStringList headers;
    for (WorkEntry::Field field : usedColumns)
        headers.append(quote(WorkEntry::fieldHeader(field)));
    stream << headers.join(QLatin1Char(',')) << "\r\n";

    for (const WorkEntry &entry : entries) {
        QStringList values;
        for (WorkEntry::Field field : usedColumns) {
            QString value;
            if (field == WorkEntry::FieldDate)
                value = entry.date.toString(Qt::ISODate);
            else if (field == WorkEntry::FieldHours)
                value = QString::number(Duration::toHours(entry.minutes), 'f', 2);
            else if (field == WorkEntry::FieldBillable)
                value = entry.billable ? QStringLiteral("Yes") : QStringLiteral("No");
            else
                value = entry.field(field).toString();
            values.append(quote(value));
        }
        stream << values.join(QLatin1Char(',')) << "\r\n";
    }

    file.close();
    return true;
}

bool CsvIo::readEntries(const QString &path,
                        QVector<WorkEntry> *entries,
                        ImportResult *result,
                        QString *error)
{
    if (!entries || !result) {
        if (error)
            *error = QStringLiteral("Internal error: no destination for the import.");
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("Cannot read %1: %2").arg(path, file.errorString());
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    const QString text = stream.readAll();
    file.close();

    const QVector<QStringList> records = parseCsv(text);
    if (records.isEmpty()) {
        if (error)
            *error = QStringLiteral("%1 is empty.").arg(path);
        return false;
    }

    // Map each column of the file onto a field, by header text or by token.
    const QStringList headerRow = records.first();
    QVector<WorkEntry::Field> columnFields;
    columnFields.reserve(headerRow.size());

    bool anyFieldRecognised = false;
    for (const QString &header : headerRow) {
        const QString cleaned = header.trimmed();
        WorkEntry::Field field = WorkEntry::fieldFromToken(cleaned);

        if (field == WorkEntry::FieldCount) {
            const QVector<WorkEntry::Field> allFields = WorkEntry::allFields();
            for (WorkEntry::Field candidate : allFields) {
                if (WorkEntry::fieldHeader(candidate).compare(cleaned, Qt::CaseInsensitive) == 0) {
                    field = candidate;
                    break;
                }
            }
        }

        if (field != WorkEntry::FieldCount)
            anyFieldRecognised = true;
        columnFields.append(field);
    }

    if (!anyFieldRecognised) {
        if (error) {
            *error = QStringLiteral(
                         "None of the columns in %1 were recognised. The first row must name "
                         "the columns, for example: Date,Issue,Summary,Duration")
                         .arg(path);
        }
        return false;
    }

    for (int recordIndex = 1; recordIndex < records.size(); ++recordIndex) {
        const QStringList record = records.at(recordIndex);
        if (record.isEmpty() || (record.size() == 1 && record.first().trimmed().isEmpty()))
            continue;

        ++result->rowsRead;

        WorkEntry entry;
        entry.source = EntrySource::Csv;

        for (int column = 0; column < record.size() && column < columnFields.size(); ++column) {
            const WorkEntry::Field field = columnFields.at(column);

            // Three columns are deliberately not taken from the file: the id
            // and the external id belong to whatever database exported it, and
            // the source records how the row entered *this* database.
            if (field == WorkEntry::FieldCount
                || field == WorkEntry::FieldId
                || field == WorkEntry::FieldExternalId
                || field == WorkEntry::FieldSource) {
                continue;
            }
            entry.setField(field, record.at(column));
        }

        QString problem;
        if (!entry.isValid(&problem)) {
            ++result->skipped;
            result->problems.append(QStringLiteral("Row %1: %2").arg(recordIndex + 1).arg(problem));
            continue;
        }

        entries->append(entry);
        ++result->imported;
    }

    return true;
}
