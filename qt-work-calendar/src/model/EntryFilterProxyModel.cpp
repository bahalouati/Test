#include "model/EntryFilterProxyModel.h"

#include "model/WorkEntryTableModel.h"

EntryFilterProxyModel::EntryFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setSortRole(WorkEntryTableModel::SortRole);
    setSortCaseSensitivity(Qt::CaseInsensitive);
    setDynamicSortFilter(true);
}

void EntryFilterProxyModel::setSearchText(const QString &text)
{
    m_searchText = text.trimmed();
    invalidateFilter();
}

void EntryFilterProxyModel::setDateRange(const QDate &from, const QDate &to)
{
    m_from = from;
    m_to = to;
    invalidateFilter();
}

void EntryFilterProxyModel::setActivityFilter(const QString &activityDisplayName)
{
    m_activity = activityDisplayName;
    invalidateFilter();
}

void EntryFilterProxyModel::setProjectFilter(const QString &project)
{
    m_project = project;
    invalidateFilter();
}

void EntryFilterProxyModel::setSprintFilter(const QString &sprint)
{
    m_sprint = sprint;
    invalidateFilter();
}

void EntryFilterProxyModel::setBillableFilter(TriState state)
{
    m_billable = state;
    invalidateFilter();
}

void EntryFilterProxyModel::clearFilters()
{
    m_searchText.clear();
    m_from = QDate();
    m_to = QDate();
    m_activity.clear();
    m_project.clear();
    m_sprint.clear();
    m_billable = Any;
    invalidateFilter();
}

int EntryFilterProxyModel::visibleMinutes() const
{
    const WorkEntryTableModel *source = qobject_cast<const WorkEntryTableModel *>(sourceModel());
    if (!source)
        return 0;

    int total = 0;
    for (int row = 0; row < rowCount(); ++row) {
        const QModelIndex sourceIndex = mapToSource(index(row, 0));
        total += source->entryAt(sourceIndex.row()).minutes;
    }
    return total;
}

QString EntryFilterProxyModel::valueOf(int sourceRow, WorkEntry::Field field) const
{
    const WorkEntryTableModel *source = qobject_cast<const WorkEntryTableModel *>(sourceModel());
    if (!source)
        return QString();
    return source->entryAt(sourceRow).field(field).toString();
}

bool EntryFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    Q_UNUSED(sourceParent)

    const WorkEntryTableModel *source = qobject_cast<const WorkEntryTableModel *>(sourceModel());
    if (!source)
        return true;

    const WorkEntry entry = source->entryAt(sourceRow);

    if (m_from.isValid() && entry.date < m_from)
        return false;
    if (m_to.isValid() && entry.date > m_to)
        return false;

    if (!m_activity.isEmpty() && activityDisplayName(entry.activity) != m_activity)
        return false;
    if (!m_project.isEmpty() && entry.effectiveProject() != m_project)
        return false;
    if (!m_sprint.isEmpty() && entry.sprint != m_sprint)
        return false;

    if (m_billable == Yes && !entry.billable)
        return false;
    if (m_billable == No && entry.billable)
        return false;

    if (!m_searchText.isEmpty()) {
        // The search looks at every field, not only the visible columns, so a
        // hidden note or branch name still finds its entry.
        bool matched = false;
        const QVector<WorkEntry::Field> fields = WorkEntry::allFields();
        for (WorkEntry::Field field : fields) {
            if (entry.field(field).toString().contains(m_searchText, Qt::CaseInsensitive)) {
                matched = true;
                break;
            }
        }
        if (!matched)
            return false;
    }

    return true;
}

bool EntryFilterProxyModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const QVariant leftValue = left.data(WorkEntryTableModel::SortRole);
    const QVariant rightValue = right.data(WorkEntryTableModel::SortRole);

    if (leftValue.userType() == QMetaType::QString && rightValue.userType() == QMetaType::QString)
        return leftValue.toString().localeAwareCompare(rightValue.toString()) < 0;

    return QSortFilterProxyModel::lessThan(left, right);
}
