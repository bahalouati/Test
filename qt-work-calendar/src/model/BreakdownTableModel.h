#ifndef WORKCALENDAR_BREAKDOWNTABLEMODEL_H
#define WORKCALENDAR_BREAKDOWNTABLEMODEL_H

#include "core/Duration.h"
#include "data/Statistics.h"

#include <QAbstractTableModel>
#include <QString>
#include <QVector>

/*!
 * \brief Shows a grouped report: one row per project, activity, sprint, ...
 *
 * The rows come ready-made from Statistics::breakdownBy(); the model only
 * formats them.
 */
class BreakdownTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    /*! The fixed columns of every breakdown. */
    enum Column {
        ColumnKey,
        ColumnTime,
        ColumnShare,
        ColumnEntries,
        ColumnBillable,

        ColumnCount
    };

    explicit BreakdownTableModel(QObject *parent = nullptr);

    /*! Replaces the rows; \a keyHeader names what the rows are grouped by. */
    void setRows(const QVector<Statistics::BreakdownRow> &rows, const QString &keyHeader);

    void setDurationFormat(Duration::Format format);

    /*! The grouping value of \a row, so a double click can filter by it. */
    QString keyAt(int row) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    QVector<Statistics::BreakdownRow> m_rows;
    QString m_keyHeader = QStringLiteral("Group");
    Duration::Format m_durationFormat = Duration::Format::HoursAndMinutes;
};

#endif // WORKCALENDAR_BREAKDOWNTABLEMODEL_H
