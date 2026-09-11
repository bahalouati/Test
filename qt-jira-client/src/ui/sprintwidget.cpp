#include "sprintwidget.h"
#include "ui_sprintwidget.h"

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

SprintWidget::SprintWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SprintWidget)
    , m_tracker(new jira::TaskTracker(this))
{
    ui->setupUi(this);

    ui->jql->setText(QSettings().value(QLatin1String(kSprintJqlKey),
                                       QLatin1String(kDefaultJql)).toString());
    ui->table->horizontalHeader()->setSectionResizeMode(SummaryColumn, QHeaderView::Stretch);

    connect(ui->refresh, &QPushButton::clicked, this, &SprintWidget::refresh);
    connect(ui->toggle, &QPushButton::clicked, this, &SprintWidget::toggleSelected);
    connect(ui->markLogged, &QPushButton::clicked, this, &SprintWidget::markLogged);
    connect(ui->remove, &QPushButton::clicked, this, &SprintWidget::removeSelected);
    connect(ui->table, &QTableWidget::itemSelectionChanged, this, &SprintWidget::selectionChanged);
    connect(ui->table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        QTableWidgetItem *item = ui->table->item(row, KeyColumn);
        if (item)
            emit issueActivated(item->text());
    });
    connect(ui->jql, &QLineEdit::editingFinished, this, [this] {
        QSettings().setValue(QLatin1String(kSprintJqlKey), ui->jql->text().trimmed());
    });

    connect(m_tracker, &jira::TaskTracker::changed, this, &SprintWidget::rebuild);
    connect(m_tracker, &jira::TaskTracker::tick, this, &SprintWidget::updateElapsed);

    m_tracker->load();
    rebuild();
}

SprintWidget::~SprintWidget()
{
    delete ui;
}

void SprintWidget::setClient(jira::Client *client)
{
    m_client = client;
}

void SprintWidget::setIdentity(const jira::User &me)
{
    m_me = me;
}

QString SprintWidget::selectedKey() const
{
    const int row = ui->table->currentRow();
    if (row < 0)
        return {};
    QTableWidgetItem *item = ui->table->item(row, KeyColumn);
    return item ? item->text() : QString();
}

void SprintWidget::refresh()
{
    if (!m_client || !m_client->isConfigured()) {
        emit errorOccurred(tr("Connect to a Jira server first."));
        return;
    }

    const QString jql = ui->jql->text().trimmed();
    if (jql.isEmpty()) {
        emit errorOccurred(tr("Enter a filter for the sprint first."));
        return;
    }

    ui->refresh->setEnabled(false);
    jira::Reply *reply = m_client->search(jql, 0, 100);
    connect(reply, &jira::Reply::succeeded, this, [this](const QJsonValue &body) {
        ui->refresh->setEnabled(true);
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
        ui->refresh->setEnabled(true);
        // openSprints() needs Jira Software; say so rather than showing a raw 400.
        QString message = error.toString();
        if (error.httpStatus == 400 && ui->jql->text().contains(QLatin1String("openSprints"))) {
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

    ui->table->setRowCount(tasks.size());
    for (int row = 0; row < tasks.size(); ++row) {
        const TrackedTask &task = tasks.at(row);

        const auto set = [this, row](int column, const QString &text) {
            auto *item = ui->table->item(row, column);
            if (!item) {
                item = new QTableWidgetItem;
                ui->table->setItem(row, column, item);
            }
            item->setText(text);
            return item;
        };

        QTableWidgetItem *key = set(KeyColumn, task.issueKey);
        QFont keyFont = key->font();
        keyFont.setBold(task.isRunning());
        key->setFont(keyFont);

        set(SummaryColumn, task.summary);
        set(SprintColumn, task.sprint.isEmpty() ? QString(QChar(0x2014)) : task.sprint);

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

    ui->table->resizeColumnsToContents();
    ui->table->horizontalHeader()->setSectionResizeMode(SummaryColumn, QHeaderView::Stretch);

    if (!selected.isEmpty()) {
        const int index = m_tracker->indexOf(selected);
        if (index >= 0)
            ui->table->selectRow(index);
    }

    const TrackedTask current = m_tracker->currentTask();
    if (current.isRunning()) {
        ui->current->setText(tr("<b>%1</b> running — %2")
                                   .arg(current.issueKey.toHtmlEscaped(),
                                        jira::formatStopwatch(current.elapsedSeconds())));
    } else {
        ui->current->setText(tr("<span style='color:gray;'>No task running</span>"));
    }
    selectionChanged();
}

void SprintWidget::updateElapsed()
{
    const QList<TrackedTask> tasks = m_tracker->tasks();
    for (int row = 0; row < tasks.size() && row < ui->table->rowCount(); ++row) {
        QTableWidgetItem *item = ui->table->item(row, TrackedColumn);
        if (item)
            item->setText(jira::formatStopwatch(tasks.at(row).elapsedSeconds()));
    }

    const TrackedTask current = m_tracker->currentTask();
    if (current.isRunning()) {
        ui->current->setText(tr("<b>%1</b> running — %2")
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

    ui->toggle->setEnabled(hasSelection);
    ui->remove->setEnabled(hasSelection);
    ui->markLogged->setEnabled(hasSelection && m_tracker->tasks().at(index).elapsedSeconds() > 0);
    ui->toggle->setText(hasSelection && m_tracker->tasks().at(index).isRunning() ? tr("Stop")
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
