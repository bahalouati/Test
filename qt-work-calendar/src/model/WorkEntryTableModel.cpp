#include "model/WorkEntryTableModel.h"

#include <QBrush>
#include <QFont>
#include <QLocale>

WorkEntryTableModel::WorkEntryTableModel(QObject *parent)
    : QAbstractTableModel(parent)
    , m_columns(WorkEntry::defaultVisibleFields())
{
}

void WorkEntryTableModel::setEntries(const QVector<WorkEntry> &entries)
{
    beginResetModel();
    m_entries = entries;
    endResetModel();
}

void WorkEntryTableModel::setColumns(const QVector<WorkEntry::Field> &columns)
{
    beginResetModel();
    m_columns = columns.isEmpty() ? WorkEntry::defaultVisibleFields() : columns;
    endResetModel();
}

QVector<WorkEntry::Field> WorkEntryTableModel::columns() const
{
    return m_columns;
}

void WorkEntryTableModel::setDurationFormat(Duration::Format format)
{
    if (m_durationFormat == format)
        return;
    m_durationFormat = format;
    if (!m_entries.isEmpty()) {
        emit dataChanged(index(0, 0),
                         index(m_entries.size() - 1, m_columns.size() - 1),
                         QVector<int>{ Qt::DisplayRole });
    }
}

WorkEntry WorkEntryTableModel::entryAt(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return WorkEntry();
    return m_entries.at(row);
}

int WorkEntryTableModel::rowOfEntryId(int id) const
{
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).id == id)
            return row;
    }
    return -1;
}

int WorkEntryTableModel::totalMinutes() const
{
    int total = 0;
    for (const WorkEntry &entry : m_entries)
        total += entry.minutes;
    return total;
}

int WorkEntryTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_entries.size();
}

int WorkEntryTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_columns.size();
}

QString WorkEntryTableModel::displayText(const WorkEntry &entry, WorkEntry::Field field) const
{
    switch (field) {
    case WorkEntry::FieldDate:
        return entry.date.isValid() ? QLocale().toString(entry.date, QLocale::ShortFormat)
                                    : QString();
    case WorkEntry::FieldDuration:
        return Duration::format(entry.minutes, m_durationFormat);
    case WorkEntry::FieldHours:
        return QString::number(Duration::toHours(entry.minutes), 'f', 2);
    case WorkEntry::FieldBillable:
        return entry.billable ? QStringLiteral("Yes") : QStringLiteral("No");
    case WorkEntry::FieldCreatedAt:
        return entry.createdAt.isValid()
                   ? QLocale().toString(entry.createdAt, QLocale::ShortFormat) : QString();
    case WorkEntry::FieldUpdatedAt:
        return entry.updatedAt.isValid()
                   ? QLocale().toString(entry.updatedAt, QLocale::ShortFormat) : QString();
    case WorkEntry::FieldDescription: {
        // Notes can be long and multi-line; the cell shows the first line only
        // and the tooltip carries the rest.
        QString text = entry.description.trimmed();
        const int newline = text.indexOf(QLatin1Char('\n'));
        if (newline >= 0)
            text = text.left(newline) + QStringLiteral(" ...");
        return text;
    }
    default:
        return entry.field(field).toString();
    }
}

QVariant WorkEntryTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size() || index.column() >= m_columns.size())
        return QVariant();

    const WorkEntry &entry = m_entries.at(index.row());
    const WorkEntry::Field field = m_columns.at(index.column());

    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return displayText(entry, field);

    case SortRole:
        // The unformatted value, so dates sort chronologically and durations
        // sort by length rather than by the text "1h 45m".
        switch (field) {
        case WorkEntry::FieldDate:      return entry.date;
        case WorkEntry::FieldDuration:
        case WorkEntry::FieldHours:     return entry.minutes;
        case WorkEntry::FieldBillable:  return entry.billable;
        case WorkEntry::FieldId:        return entry.id;
        case WorkEntry::FieldCreatedAt: return entry.createdAt;
        case WorkEntry::FieldUpdatedAt: return entry.updatedAt;
        default:                        return displayText(entry, field);
        }

    case EntryIdRole:
        return entry.id;

    case FieldRole:
        return static_cast<int>(field);

    case Qt::TextAlignmentRole:
        if (field == WorkEntry::FieldDuration || field == WorkEntry::FieldHours
            || field == WorkEntry::FieldId) {
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        }
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);

    case Qt::ToolTipRole: {
        QStringList lines;
        lines << entry.title();
        lines << QStringLiteral("%1 on %2")
                     .arg(Duration::format(entry.minutes, m_durationFormat),
                          QLocale().toString(entry.date, QLocale::LongFormat));
        lines << QStringLiteral("%1 - %2")
                     .arg(activityDisplayName(entry.activity),
                          entryStatusDisplayName(entry.status));
        if (!entry.description.trimmed().isEmpty())
            lines << QString() << entry.description.trimmed();
        return lines.join(QLatin1Char('\n'));
    }

    case Qt::FontRole:
        if (!entry.billable) {
            QFont font;
            font.setItalic(true);
            return font;
        }
        return QVariant();

    default:
        return QVariant();
    }
}

QVariant WorkEntryTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && section >= 0 && section < m_columns.size()) {
        if (role == Qt::DisplayRole)
            return WorkEntry::fieldHeader(m_columns.at(section));
        if (role == FieldRole)
            return static_cast<int>(m_columns.at(section));
    }
    if (orientation == Qt::Vertical && role == Qt::DisplayRole)
        return section + 1;
    return QVariant();
}
