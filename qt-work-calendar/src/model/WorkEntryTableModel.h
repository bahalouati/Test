#ifndef WORKCALENDAR_WORKENTRYTABLEMODEL_H
#define WORKCALENDAR_WORKENTRYTABLEMODEL_H

#include "core/Duration.h"
#include "core/WorkEntry.h"

#include <QAbstractTableModel>
#include <QVector>

/*!
 * \brief Shows a list of work entries as a table.
 *
 * The model holds a copy of the entries it was given; it never reads the
 * database itself. The window refills it after every change, which keeps the
 * data flow one-directional and easy to follow: database -> window -> model.
 *
 * Which columns exist is configurable at run time through setColumns(), so the
 * user can hide the fields they do not use without the model knowing anything
 * about the user interface.
 */
class WorkEntryTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    /*! Roles beyond the standard ones, used by the proxy and the window. */
    enum CustomRole {
        SortRole = Qt::UserRole + 1, //!< the raw value, so dates and numbers sort properly
        EntryIdRole,                 //!< the database id of the row's entry
        FieldRole                    //!< the WorkEntry::Field a column shows
    };

    explicit WorkEntryTableModel(QObject *parent = nullptr);

    /*! Replaces the contents of the table. */
    void setEntries(const QVector<WorkEntry> &entries);

    /*! Chooses the visible columns and their order. */
    void setColumns(const QVector<WorkEntry::Field> &columns);
    QVector<WorkEntry::Field> columns() const;

    /*! How durations are rendered in the Duration column. */
    void setDurationFormat(Duration::Format format);

    /*! The entry shown in \a row, or a default entry when \a row is out of range. */
    WorkEntry entryAt(int row) const;

    /*! The row showing the entry with \a id, or -1. */
    int rowOfEntryId(int id) const;

    /*! Sum of the durations of every row currently in the model. */
    int totalMinutes() const;

    // QAbstractTableModel
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    /*! The text shown for \a field of \a entry. */
    QString displayText(const WorkEntry &entry, WorkEntry::Field field) const;

    QVector<WorkEntry> m_entries;
    QVector<WorkEntry::Field> m_columns;
    Duration::Format m_durationFormat = Duration::Format::HoursAndMinutes;
};

#endif // WORKCALENDAR_WORKENTRYTABLEMODEL_H
