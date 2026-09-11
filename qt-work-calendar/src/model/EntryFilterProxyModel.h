#ifndef WORKCALENDAR_ENTRYFILTERPROXYMODEL_H
#define WORKCALENDAR_ENTRYFILTERPROXYMODEL_H

#include "core/WorkEntry.h"

#include <QDate>
#include <QSortFilterProxyModel>
#include <QString>

/*!
 * \brief Sorts and filters the entry table.
 *
 * Each criterion has its own setter and can be switched off independently; a
 * row must pass all of the active ones. Sorting uses WorkEntryTableModel's
 * SortRole, so a duration column sorts by length and a date column by date
 * rather than by their printed text.
 */
class EntryFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    /*! The three states of a yes/no filter. */
    enum TriState {
        Any,
        Yes,
        No
    };

    explicit EntryFilterProxyModel(QObject *parent = nullptr);

    /*! Free text, matched case-insensitively against every column. */
    void setSearchText(const QString &text);

    /*! Restricts to a date range; pass invalid dates to switch a bound off. */
    void setDateRange(const QDate &from, const QDate &to);

    /*! Empty means "any"; otherwise the displayed value must match exactly. */
    void setActivityFilter(const QString &activityDisplayName);
    void setProjectFilter(const QString &project);
    void setSprintFilter(const QString &sprint);
    void setBillableFilter(TriState state);

    /*! Drops every criterion. */
    void clearFilters();

    /*! Sum of the durations of the rows that currently pass the filters. */
    int visibleMinutes() const;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    /*! The displayed text of \a field for the given source row. */
    QString valueOf(int sourceRow, WorkEntry::Field field) const;

    QString m_searchText;
    QDate m_from;
    QDate m_to;
    QString m_activity;
    QString m_project;
    QString m_sprint;
    TriState m_billable = Any;
};

#endif // WORKCALENDAR_ENTRYFILTERPROXYMODEL_H
