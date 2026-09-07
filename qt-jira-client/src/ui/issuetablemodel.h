#pragma once

#include "core/jiratypes.h"

#include <QAbstractTableModel>
#include <QList>

// Read-only table over one page of search results.
class IssueTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    // Sorting on the display string gets "Updated" and issue keys wrong; the
    // view sorts on this role instead.
    static constexpr int SortRole = Qt::UserRole + 1;

    enum Column {
        KeyColumn,
        TypeColumn,
        StatusColumn,
        PriorityColumn,
        SummaryColumn,
        AssigneeColumn,
        UpdatedColumn,
        ColumnCount
    };

    explicit IssueTableModel(QObject *parent = nullptr);

    void setIssues(const QList<jira::Issue> &issues);
    jira::Issue issueAt(int row) const;
    int indexOfKey(const QString &key) const;
    void replaceIssue(const jira::Issue &issue);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    QList<jira::Issue> m_issues;
};
