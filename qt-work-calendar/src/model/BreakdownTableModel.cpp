#include "model/BreakdownTableModel.h"

BreakdownTableModel::BreakdownTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void BreakdownTableModel::setRows(const QVector<Statistics::BreakdownRow> &rows,
                                  const QString &keyHeader)
{
    beginResetModel();
    m_rows = rows;
    m_keyHeader = keyHeader;
    endResetModel();
}

void BreakdownTableModel::setDurationFormat(Duration::Format format)
{
    if (m_durationFormat == format)
        return;
    beginResetModel();
    m_durationFormat = format;
    endResetModel();
}

QString BreakdownTableModel::keyAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return QString();
    return m_rows.at(row).key;
}

int BreakdownTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_rows.size();
}

int BreakdownTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return ColumnCount;
}

QVariant BreakdownTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return QVariant();

    const Statistics::BreakdownRow &row = m_rows.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColumnKey:      return row.key;
        case ColumnTime:     return Duration::format(row.minutes, m_durationFormat);
        case ColumnShare:    return QStringLiteral("%1 %").arg(row.share * 100.0, 0, 'f', 1);
        case ColumnEntries:  return row.entryCount;
        case ColumnBillable: return Duration::format(row.billableMinutes, m_durationFormat);
        default:             return QVariant();
        }
    }

    if (role == Qt::TextAlignmentRole && index.column() != ColumnKey)
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);

    if (role == Qt::ToolTipRole && index.column() == ColumnKey)
        return row.key;

    return QVariant();
}

QVariant BreakdownTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();

    switch (section) {
    case ColumnKey:      return m_keyHeader;
    case ColumnTime:     return QStringLiteral("Time");
    case ColumnShare:    return QStringLiteral("Share");
    case ColumnEntries:  return QStringLiteral("Entries");
    case ColumnBillable: return QStringLiteral("Billable");
    default:             return QVariant();
    }
}
