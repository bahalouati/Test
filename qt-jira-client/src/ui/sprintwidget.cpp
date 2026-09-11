#include "sprintwidget.h"

#include "core/jiraclient.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

using jira::TrackedTask;

namespace {
constexpr auto kSprintJqlKey = "sprint/jql";
// Everything still on my plate in a sprint that is open. Editable, because
// openSprints() needs Jira Software and not every instance has it.
constexpr auto kDefaultJql = "assignee = currentUser() AND sprint in openSprints()";

enum Column { KeyColumn, SummaryColumn, SprintColumn, StatusColumn, TrackedColumn, ColumnCount };
} // namespace

SprintWidget::SprintWidget(jira::Client *client, QWidget *parent)
    : QWidget(parent)
    , m_client(client)
    , m_tracker(new jira::TaskTracker(this))
{
    m_jql = new QLineEdit(this);
    m_jql->setText(QSettings().value(QLatin1String(kSprintJqlKey), QLatin1String(kDefaultJql)).toString());
    m_jql->setToolTip(tr("Which issues belong on this list. A task drops off it when Jira stops "
                         "assigning the issue to you."));

    m_refresh = new QPushButton(tr("Refresh"), this);
    m_toggle = new QPushButton(tr("Start"), this);
    m_toggle->setEnabled(false);
    m_markLogged = new QPushButton(tr("Mark logged"), this);
    m_markLogged->setEnabled(false);
    m_markLogged->setToolTip(tr("Clears the tracked time once you have written the real worklog "
                                "into Jira yourself."));
    m_remove = new QPushButton(tr("Remove"), this);
    m_remove->setEnabled(false);

    auto *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Sprint filter:"), this));
    controls->addWidget(m_jql, 1);
    controls->addWidget(m_refresh);

    m_current = new QLabel(this);
    m_current->setTextFormat(Qt::RichText);

    auto *actions = new QHBoxLayout;
    actions->addWidget(m_toggle);
    actions->addWidget(m_markLogged);
    actions->addWidget(m_remove);
    actions->addStretch();
    actions->addWidget(m_current);

    m_table = new QTableWidget(0, ColumnCount, this);
    m_table->setHorizontalHeaderLabels({tr("Issue"), tr("Summary"), tr("Sprint"),
                                        tr("Status"), tr("Tracked")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setSectionResizeMode(SummaryColumn, QHeaderView::Stretch);

    auto *note = new QLabel(tr("Tracked time is kept on this computer only — nothing is written to "
                               "Jira. Log the real figure yourself, then use <b>Mark logged</b> to "
                               "clear it."), this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: gray;"));

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(controls);
    layout->addLayout(actions);
    layout->addWidget(m_table, 1);
    layout->addWidget(note);

    connect(m_refresh, &QPushButton::clicked, this, &SprintWidget::refresh);
    connect(m_toggle, &QPushButton::clicked, this, &SprintWidget::toggleSelected);
    connect(m_markLogged, &QPushButton::clicked, this, &SprintWidget::markLogged);
    connect(m_remove, &QPushButton::clicked, this, &SprintWidget::removeSelected);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &SprintWidget::selectionChanged);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        QTableWidgetItem *item = m_table->item(row, KeyColumn);
        if (item)
            emit issueActivated(item->text());
    });
    connect(m_jql, &QLineEdit::editingFinished, this, [this] {
        QSettings().setValue(QLatin1String(kSprintJqlKey), m_jql->text().trimmed());
    });

    connect(m_tracker, &jira::TaskTracker::changed, this, &SprintWidget::rebuild);
    connect(m_tracker, &jira::TaskTracker::tick, this, &SprintWidget::updateElapsed);

    m_tracker->load();
    rebuild();
}

void SprintWidget::setIdentity(const jira::User &me)
{
    m_me = me;
}

QString SprintWidget::selectedKey() const
{
    const int row = m_table->currentRow();
    if (row < 0)
        return {};
    QTableWidgetItem *item = m_table->item(row, KeyColumn);
    return item ? item->text() : QString();
}

void SprintWidget::refresh()
{
    if (!m_client->isConfigured()) {
        emit errorOccurred(tr("Connect to a Jira server first."));
        return;
    }

    const QString jql = m_jql->text().trimmed();
    if (jql.isEmpty()) {
        emit errorOccurred(tr("Enter a filter for the sprint first."));
        return;
    }

    m_refresh->setEnabled(false);
    jira::Reply *reply = m_client->search(jql, 0, 100);
    connect(reply, &jira::Reply::succeeded, this, [this](const QJsonValue &body) {
        m_refresh->setEnabled(true);
        const jira::SearchResult result = jira::SearchResult::fromJson(body.toObject());

        // "Done" means Jira no longer assigns it to me, so the assignee on each
        // returned issue is what decides whether it stays.
        QSet<QString> stillMine;
        for (const jira::Issue &issue : result.issues) {
            const jira::User &assignee = issue.assignee;
            bool mine = false;
            if (!m_me.accountId.isEmpty() && !assignee.accountId.isEmpty())
                mine = assignee.accountId == m_me.accountId;
            else if (!m_me.name.isEmpty() && !assignee.name.isEmpty())
                mine = assignee.name.compare(m_me.name, Qt::CaseInsensitive) == 0;
            else
                mine = !assignee.isNull();
            if (mine)
                stillMine.insert(issue.key);
        }

        m_tracker->merge(result.issues, stillMine);
        emit statusMessage(tr("%1 task(s) in the sprint.").arg(stillMine.size()));
    });
    connect(reply, &jira::Reply::failed, this, [this](const jira::Error &error) {
        m_refresh->setEnabled(true);
        // openSprints() needs Jira Software; say so rather than showing a raw 400.
        QString message = error.toString();
        if (error.httpStatus == 400 && m_jql->text().contains(QLatin1String("openSprints"))) {
            message += QLatin1Char('\n')
                    + tr("If this instance has no Jira Software, replace the filter with one that "
                         "does not use openSprints(), for example: "
                         "assignee = currentUser() AND resolution = Unresolved");
        }
        emit errorOccurred(message);
    });
}

void SprintWidget::rebuild()
{
    const QString selected = selectedKey();
    const QList<TrackedTask> tasks = m_tracker->tasks();

    m_table->setRowCount(tasks.size());
    for (int row = 0; row < tasks.size(); ++row) {
        const TrackedTask &task = tasks.at(row);

        const auto set = [this, row](int column, const QString &text) {
            auto *item = m_table->item(row, column);
            if (!item) {
                item = new QTableWidgetItem;
                m_table->setItem(row, column, item);
            }
            item->setText(text);
            return item;
        };

        QTableWidgetItem *key = set(KeyColumn, task.issueKey);
        QFont keyFont = key->font();
        keyFont.setBold(task.isRunning());
        key->setFont(keyFont);

        set(SummaryColumn, task.summary);
        set(SprintColumn, task.sprint.isEmpty() ? QStringLiteral("—") : task.sprint);

        QTableWidgetItem *status = set(StatusColumn, task.unassigned
                                                             ? tr("no longer yours")
                                                             : task.status);
        status->setForeground(task.unassigned ? QColor(QStringLiteral("#b3261e")) : QColor());
        if (task.unassigned) {
            status->setToolTip(tr("Jira no longer assigns this to you, so it counts as done. It is "
                                  "still listed because it holds tracked time — remove it once you "
                                  "have logged that."));
        }

        QTableWidgetItem *tracked = set(TrackedColumn, jira::formatStopwatch(task.elapsedSeconds()));
        QFont trackedFont = tracked->font();
        trackedFont.setBold(task.isRunning());
        tracked->setFont(trackedFont);
        tracked->setForeground(task.isRunning() ? QColor(QStringLiteral("#1e7d32")) : QColor());
    }

    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setSectionResizeMode(SummaryColumn, QHeaderView::Stretch);

    if (!selected.isEmpty()) {
        const int index = m_tracker->indexOf(selected);
        if (index >= 0)
            m_table->selectRow(index);
    }

    const TrackedTask current = m_tracker->currentTask();
    if (current.isRunning()) {
        m_current->setText(tr("<b>%1</b> running — %2")
                                   .arg(current.issueKey.toHtmlEscaped(),
                                        jira::formatStopwatch(current.elapsedSeconds())));
    } else {
        m_current->setText(tr("<span style='color:gray;'>No task running</span>"));
    }
    selectionChanged();
}

void SprintWidget::updateElapsed()
{
    const QList<TrackedTask> tasks = m_tracker->tasks();
    for (int row = 0; row < tasks.size() && row < m_table->rowCount(); ++row) {
        QTableWidgetItem *item = m_table->item(row, TrackedColumn);
        if (item)
            item->setText(jira::formatStopwatch(tasks.at(row).elapsedSeconds()));
    }

    const TrackedTask current = m_tracker->currentTask();
    if (current.isRunning()) {
        m_current->setText(tr("<b>%1</b> running — %2")
                                   .arg(current.issueKey.toHtmlEscaped(),
                                        jira::formatStopwatch(current.elapsedSeconds())));
    }

    // "Mark logged" turns on the moment there is something to clear, rather
    // than waiting for the next full rebuild of the table.
    selectionChanged();
}

void SprintWidget::selectionChanged()
{
    const QString key = selectedKey();
    const int index = key.isEmpty() ? -1 : m_tracker->indexOf(key);
    const bool hasSelection = index >= 0;

    m_toggle->setEnabled(hasSelection);
    m_remove->setEnabled(hasSelection);
    m_markLogged->setEnabled(hasSelection && m_tracker->tasks().at(index).elapsedSeconds() > 0);
    m_toggle->setText(hasSelection && m_tracker->tasks().at(index).isRunning() ? tr("Stop")
                                                                              : tr("Start"));
}

void SprintWidget::toggleSelected()
{
    const QString key = selectedKey();
    if (key.isEmpty())
        return;
    m_tracker->toggle(key);
    const int index = m_tracker->indexOf(key);
    if (index >= 0 && m_tracker->tasks().at(index).isRunning())
        emit statusMessage(tr("Timing %1.").arg(key));
    else
        emit statusMessage(tr("Stopped %1.").arg(key));
}

void SprintWidget::markLogged()
{
    const QString key = selectedKey();
    const int index = key.isEmpty() ? -1 : m_tracker->indexOf(key);
    if (index < 0)
        return;

    const TrackedTask task = m_tracker->tasks().at(index);
    const QString tracked = jira::formatTrackedDuration(task.elapsedSeconds());

    const QMessageBox::StandardButton answer = QMessageBox::question(
            this, tr("Mark logged"),
            tr("Clear the %1 tracked against %2?\n\nThis only resets the local stopwatch — it does "
               "not write anything to Jira.")
                    .arg(tracked, key),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes)
        return;

    // The figure is what you would type into Jira, so put it on the clipboard.
    QApplication::clipboard()->setText(tracked);
    m_tracker->resetTracked(key);
    emit statusMessage(tr("Cleared %1 on %2 — the figure is on the clipboard.").arg(tracked, key));
}

void SprintWidget::removeSelected()
{
    const QString key = selectedKey();
    const int index = key.isEmpty() ? -1 : m_tracker->indexOf(key);
    if (index < 0)
        return;

    const TrackedTask task = m_tracker->tasks().at(index);
    if (task.elapsedSeconds() > 0) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
                this, tr("Remove task"),
                tr("%1 still has %2 tracked against it. Remove it anyway?")
                        .arg(key, jira::formatTrackedDuration(task.elapsedSeconds())),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }
    m_tracker->remove(key);
    emit statusMessage(tr("Removed %1.").arg(key));
}
