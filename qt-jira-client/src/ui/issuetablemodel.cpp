#include "issuetablemodel.h"

#include <QBrush>
#include <QFont>
#include <QLocale>

IssueTableModel::IssueTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void IssueTableModel::setIssues(const QList<jira::Issue> &issues)
{
    beginResetModel();
    m_issues = issues;
    endResetModel();
}

jira::Issue IssueTableModel::issueAt(int row) const
{
    if (row < 0 || row >= m_issues.size())
        return {};
    return m_issues.at(row);
}

int IssueTableModel::indexOfKey(const QString &key) const
{
    for (int row = 0; row < m_issues.size(); ++row) {
        if (m_issues.at(row).key == key)
            return row;
    }
    return -1;
}

void IssueTableModel::replaceIssue(const jira::Issue &issue)
{
    const int row = indexOfKey(issue.key);
    if (row < 0)
        return;
    m_issues[row] = issue;
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

int IssueTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_issues.size();
}

int IssueTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant IssueTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_issues.size())
        return {};

    const jira::Issue &issue = m_issues.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
    case Qt::ToolTipRole:
        switch (index.column()) {
        case KeyColumn:
            return issue.key;
        case TypeColumn:
            return issue.issueType;
        case StatusColumn:
            return issue.status;
        case PriorityColumn:
            return issue.priority;
        case SummaryColumn:
            return issue.summary;
        case AssigneeColumn:
            return issue.assignee.isNull() ? tr("Unassigned") : issue.assignee.label();
        case UpdatedColumn:
            return issue.updated.isValid()
                    ? QLocale().toString(issue.updated.toLocalTime(), QLocale::ShortFormat)
                    : QStringLiteral("-");
        default:
            break;
        }
        return {};

    case SortRole:
        switch (index.column()) {
        case KeyColumn:
            return jira::issueSortKey(issue.key);
        case UpdatedColumn:
            return issue.updated;
        default:
            return data(index, Qt::DisplayRole);
        }

    case Qt::FontRole:
        if (index.column() == KeyColumn) {
            QFont font;
            font.setBold(true);
            return font;
        }
        return {};

    case Qt::ForegroundRole:
        // Resolved rows recede; everything else keeps the palette default.
        if (issue.isResolved())
            return QBrush(Qt::gray);
        return {};

    case Qt::TextAlignmentRole:
        if (index.column() == UpdatedColumn)
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        return {};

    default:
        return {};
    }
}

QVariant IssueTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QAbstractTableModel::headerData(section, orientation, role);

    switch (section) {
    case KeyColumn:
        return tr("Key");
    case TypeColumn:
        return tr("Type");
    case StatusColumn:
        return tr("Status");
    case PriorityColumn:
        return tr("Priority");
    case SummaryColumn:
        return tr("Summary");
    case AssigneeColumn:
        return tr("Assignee");
    case UpdatedColumn:
        return tr("Updated");
    default:
        return {};
    }
}
