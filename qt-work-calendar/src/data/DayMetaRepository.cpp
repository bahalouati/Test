#include "data/DayMetaRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

/*!
 * \brief Turns a null QString into an empty one.
 *
 * QSqlQuery binds a null QString as SQL NULL, which the NOT NULL note column
 * rejects - so a day type set without a note would fail to save.
 */
QString text(const QString &value)
{
    return value.isNull() ? QString(QLatin1String("")) : value;
}

} // namespace

DayMetaRepository::DayMetaRepository(Database &database)
    : m_database(database)
{
}

DayMeta DayMetaRepository::forDate(const QDate &date) const
{
    DayMeta meta;
    meta.date = date;

    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral(
        "SELECT day_type, target_minutes, note FROM day_meta WHERE date = :date"));
    query.bindValue(QStringLiteral(":date"), date.toString(Qt::ISODate));
    if (!query.exec() || !query.next())
        return meta;

    meta.type = dayTypeFromString(query.value(0).toString());
    meta.targetMinutesOverride = query.value(1).toInt();
    meta.note = query.value(2).toString();
    return meta;
}

QHash<QDate, DayMeta> DayMetaRepository::inRange(const QDate &from, const QDate &to) const
{
    QHash<QDate, DayMeta> records;

    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral(
        "SELECT date, day_type, target_minutes, note FROM day_meta "
        "WHERE date BETWEEN :from AND :to"));
    query.bindValue(QStringLiteral(":from"), from.toString(Qt::ISODate));
    query.bindValue(QStringLiteral(":to"), to.toString(Qt::ISODate));
    if (!query.exec())
        return records;

    while (query.next()) {
        DayMeta meta;
        meta.date = QDate::fromString(query.value(0).toString(), Qt::ISODate);
        if (!meta.date.isValid())
            continue;
        meta.type = dayTypeFromString(query.value(1).toString());
        meta.targetMinutesOverride = query.value(2).toInt();
        meta.note = query.value(3).toString();
        records.insert(meta.date, meta);
    }
    return records;
}

bool DayMetaRepository::save(const DayMeta &meta, QString *error)
{
    if (!meta.date.isValid()) {
        if (error)
            *error = QStringLiteral("The day record has no date.");
        return false;
    }

    if (meta.isEmpty())
        return remove(meta.date, error);

    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral(
        // "excluded" refers to the row the INSERT tried to add, so each
        // placeholder appears exactly once - some drivers cannot bind a named
        // placeholder that is used twice.
        "INSERT INTO day_meta (date, day_type, target_minutes, note) "
        "VALUES (:date, :day_type, :target_minutes, :note) "
        "ON CONFLICT(date) DO UPDATE SET "
        "day_type = excluded.day_type, "
        "target_minutes = excluded.target_minutes, "
        "note = excluded.note"));
    query.bindValue(QStringLiteral(":date"), meta.date.toString(Qt::ISODate));
    query.bindValue(QStringLiteral(":day_type"), dayTypeToString(meta.type));
    query.bindValue(QStringLiteral(":target_minutes"), meta.targetMinutesOverride);
    query.bindValue(QStringLiteral(":note"), text(meta.note));

    if (!query.exec()) {
        if (error)
            *error = query.lastError().text();
        return false;
    }
    return true;
}

bool DayMetaRepository::remove(const QDate &date, QString *error)
{
    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral("DELETE FROM day_meta WHERE date = :date"));
    query.bindValue(QStringLiteral(":date"), date.toString(Qt::ISODate));
    if (!query.exec()) {
        if (error)
            *error = query.lastError().text();
        return false;
    }
    return true;
}

int DayMetaRepository::absenceDayCount(const QDate &from, const QDate &to) const
{
    const QHash<QDate, DayMeta> records = inRange(from, to);
    int count = 0;
    QHash<QDate, DayMeta>::const_iterator it = records.constBegin();
    for (; it != records.constEnd(); ++it) {
        if (!dayTypeExpectsWork(it.value().type))
            ++count;
    }
    return count;
}
