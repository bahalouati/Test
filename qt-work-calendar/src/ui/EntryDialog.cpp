#include "ui/EntryDialog.h"

#include "ui_EntryDialog.h"

#include "core/Duration.h"

#include <QDesktopServices>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>

EntryDialog::EntryDialog(const WorkEntry &entry,
                         const Settings &settings,
                         const EntrySuggestions &suggestions,
                         QWidget *parent)
    : QDialog(parent)
    , m_ui(new Ui::EntryDialog)
    , m_settings(settings)
    , m_suggestions(suggestions)
    , m_originalEntry(entry)
    , m_resultEntry(entry)
{
    m_ui->setupUi(this);

    setWindowTitle(entry.id < 0 ? QStringLiteral("New work entry")
                                : QStringLiteral("Edit work entry"));

    const QVector<Activity> activities = allActivities();
    for (Activity activity : activities)
        m_ui->comboActivity->addItem(activityDisplayName(activity), activityToString(activity));

    const QVector<EntryStatus> statuses = allEntryStatuses();
    for (EntryStatus status : statuses)
        m_ui->comboStatus->addItem(entryStatusDisplayName(status), entryStatusToString(status));

    const QVector<Priority> priorities = allPriorities();
    for (Priority priority : priorities)
        m_ui->comboPriority->addItem(priorityDisplayName(priority), priorityToString(priority));

    const QVector<WorkLocation> locations = allWorkLocations();
    for (WorkLocation location : locations)
        m_ui->comboLocation->addItem(workLocationDisplayName(location), workLocationToString(location));

    populateChoices();
    loadEntry(entry);

    connect(m_ui->lineDuration, &QLineEdit::textChanged,
            this, &EntryDialog::onDurationTextChanged);
    connect(m_ui->checkUseTimes, &QCheckBox::toggled, this, &EntryDialog::onUseTimesToggled);
    connect(m_ui->timeStart, &QTimeEdit::timeChanged, this, &EntryDialog::onStartTimeChanged);
    connect(m_ui->timeEnd, &QTimeEdit::timeChanged, this, &EntryDialog::onEndTimeChanged);
    connect(m_ui->dateEdit, &QDateEdit::dateChanged, this, &EntryDialog::onDateChanged);

    connect(m_ui->buttonQuarterHour, &QPushButton::clicked, this, &EntryDialog::onAddQuarterHour);
    connect(m_ui->buttonHalfHour, &QPushButton::clicked, this, &EntryDialog::onAddHalfHour);
    connect(m_ui->buttonOneHour, &QPushButton::clicked, this, &EntryDialog::onAddOneHour);
    connect(m_ui->buttonHalfDay, &QPushButton::clicked, this, &EntryDialog::onSetHalfDay);
    connect(m_ui->buttonFullDay, &QPushButton::clicked, this, &EntryDialog::onSetFullDay);
    connect(m_ui->buttonRestOfDay, &QPushButton::clicked, this, &EntryDialog::onFillTheDay);

    connect(m_ui->comboIssueKey, &QComboBox::activated, this, &EntryDialog::onIssueKeyActivated);
    connect(m_ui->buttonFillFromRecent, &QPushButton::clicked,
            this, &EntryDialog::onFillFromRecentClicked);

    connect(m_ui->buttonOpenIssueUrl, &QPushButton::clicked, this, &EntryDialog::onOpenIssueUrl);
    connect(m_ui->buttonOpenMergeRequest, &QPushButton::clicked,
            this, &EntryDialog::onOpenMergeRequest);
    connect(m_ui->buttonOpenTestsheet, &QPushButton::clicked, this, &EntryDialog::onOpenTestsheet);

    connect(m_ui->buttonBox, &QDialogButtonBox::accepted, this, &EntryDialog::onAccepted);
    connect(m_ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateDurationHint();
}

EntryDialog::~EntryDialog()
{
    delete m_ui;
}

void EntryDialog::populateChoices()
{
    m_ui->comboIssueKey->addItem(QString());
    for (const WorkEntry &recent : m_suggestions.recentIssues) {
        // The list shows enough to tell two similar keys apart, but the value
        // written into the field is the key alone.
        m_ui->comboIssueKey->addItem(
            QStringLiteral("%1  -  %2").arg(recent.issueKey, recent.summary).trimmed(),
            recent.issueKey);
    }

    m_ui->comboIssueType->addItems(QStringList() << QString() << m_suggestions.issueTypes);
    m_ui->comboEpic->addItems(QStringList() << QString() << m_suggestions.epics);
    m_ui->comboSprint->addItems(QStringList() << QString() << m_suggestions.sprints);
    m_ui->comboComponent->addItems(QStringList() << QString() << m_suggestions.components);
    m_ui->comboFixVersion->addItems(QStringList() << QString() << m_suggestions.fixVersions);
    m_ui->comboBranch->addItems(QStringList() << QString() << m_suggestions.branches);
}

void EntryDialog::loadEntry(const WorkEntry &entry)
{
    m_ui->dateEdit->setDate(entry.date.isValid() ? entry.date : QDate::currentDate());

    const bool hasTimes = entry.startTime.isValid();
    m_ui->checkUseTimes->setChecked(hasTimes);
    m_ui->timeStart->setEnabled(hasTimes);
    m_ui->timeEnd->setEnabled(hasTimes);
    m_ui->checkDurationFromTimes->setEnabled(hasTimes);
    m_ui->timeStart->setTime(hasTimes ? entry.startTime : m_settings.defaultStartTime());
    if (entry.endTime.isValid())
        m_ui->timeEnd->setTime(entry.endTime);
    else
        m_ui->timeEnd->setTime(m_ui->timeStart->time().addSecs(3600));

    // An entry that is being created starts empty rather than at zero minutes,
    // so the field shows its placeholder and invites a value.
    if (entry.minutes > 0)
        setMinutes(entry.minutes);

    m_ui->comboActivity->setCurrentIndex(
        m_ui->comboActivity->findData(activityToString(entry.activity)));
    m_ui->comboStatus->setCurrentIndex(
        m_ui->comboStatus->findData(entryStatusToString(entry.status)));
    m_ui->comboPriority->setCurrentIndex(
        m_ui->comboPriority->findData(priorityToString(entry.priority)));
    m_ui->comboLocation->setCurrentIndex(
        m_ui->comboLocation->findData(workLocationToString(entry.location)));
    m_ui->checkBillable->setChecked(entry.billable);

    m_ui->comboIssueKey->setCurrentText(entry.issueKey);
    m_ui->lineSummary->setText(entry.summary);
    m_ui->lineProject->setText(entry.project);
    m_ui->comboIssueType->setCurrentText(entry.issueType);
    m_ui->comboEpic->setCurrentText(entry.epic);
    m_ui->comboSprint->setCurrentText(entry.sprint);
    m_ui->comboComponent->setCurrentText(entry.component);
    m_ui->comboFixVersion->setCurrentText(entry.fixVersion);

    m_ui->lineIssueUrl->setText(entry.issueUrl);
    m_ui->lineMergeRequest->setText(entry.mergeRequest);
    m_ui->comboBranch->setCurrentText(entry.branch);
    m_ui->lineTestsheetName->setText(entry.testsheetName);
    m_ui->lineTestsheetUrl->setText(entry.testsheetUrl);

    m_ui->textDescription->setPlainText(entry.description);
    m_ui->lineTags->setText(entry.tags);
}

WorkEntry EntryDialog::entry() const
{
    return m_resultEntry;
}

WorkEntry EntryDialog::buildEntry() const
{
    // Start from the original so the id, the source and the creation time - none
    // of which the user edits - survive the round trip.
    WorkEntry entry = m_originalEntry;

    entry.date = m_ui->dateEdit->date();
    entry.minutes = currentMinutes();

    if (m_ui->checkUseTimes->isChecked()) {
        entry.startTime = m_ui->timeStart->time();
        entry.endTime = m_ui->timeEnd->time();
    } else {
        entry.startTime = QTime();
        entry.endTime = QTime();
    }

    entry.activity = activityFromString(m_ui->comboActivity->currentData().toString());
    entry.status = entryStatusFromString(m_ui->comboStatus->currentData().toString());
    entry.priority = priorityFromString(m_ui->comboPriority->currentData().toString());
    entry.location = workLocationFromString(m_ui->comboLocation->currentData().toString());
    entry.billable = m_ui->checkBillable->isChecked();

    entry.issueKey = m_ui->comboIssueKey->currentText().trimmed();
    entry.summary = m_ui->lineSummary->text().trimmed();
    entry.project = m_ui->lineProject->text().trimmed();
    entry.issueType = m_ui->comboIssueType->currentText().trimmed();
    entry.epic = m_ui->comboEpic->currentText().trimmed();
    entry.sprint = m_ui->comboSprint->currentText().trimmed();
    entry.component = m_ui->comboComponent->currentText().trimmed();
    entry.fixVersion = m_ui->comboFixVersion->currentText().trimmed();

    entry.issueUrl = m_ui->lineIssueUrl->text().trimmed();
    entry.mergeRequest = m_ui->lineMergeRequest->text().trimmed();
    entry.branch = m_ui->comboBranch->currentText().trimmed();
    entry.testsheetName = m_ui->lineTestsheetName->text().trimmed();
    entry.testsheetUrl = m_ui->lineTestsheetUrl->text().trimmed();

    entry.description = m_ui->textDescription->toPlainText().trimmed();
    entry.tags = m_ui->lineTags->text().trimmed();

    return entry;
}

int EntryDialog::currentMinutes(bool *ok) const
{
    return Duration::parseMinutes(m_ui->lineDuration->text(), ok);
}

void EntryDialog::setMinutes(int minutes)
{
    m_updatingDuration = true;
    m_ui->lineDuration->setText(Duration::format(qMax(0, minutes)));
    m_updatingDuration = false;
    updateDurationHint();
}

void EntryDialog::addMinutes(int minutes)
{
    setMinutes(currentMinutes() + minutes);
}

void EntryDialog::updateDurationHint()
{
    const QString text = m_ui->lineDuration->text().trimmed();
    if (text.isEmpty()) {
        m_ui->labelDurationHint->setText(QStringLiteral("how long did it take?"));
        return;
    }

    bool ok = false;
    const int minutes = Duration::parseMinutes(text, &ok);
    if (!ok) {
        m_ui->labelDurationHint->setText(QStringLiteral("not understood"));
        return;
    }

    QString hint = QStringLiteral("%1 = %2 h")
                       .arg(Duration::format(minutes))
                       .arg(Duration::toHours(minutes), 0, 'f', 2);

    const int increment = m_settings.roundingIncrementMinutes();
    const int rounded = Duration::roundUpTo(minutes, increment);
    if (rounded != minutes)
        hint += QStringLiteral(", saved as %1").arg(Duration::format(rounded));

    m_ui->labelDurationHint->setText(hint);
}

void EntryDialog::onDurationTextChanged(const QString &)
{
    updateDurationHint();

    // Typing a duration by hand means the start/end times are no longer the
    // source of truth for it.
    if (!m_updatingDuration && m_ui->checkDurationFromTimes->isChecked()
        && m_ui->checkUseTimes->isChecked()) {
        m_ui->checkDurationFromTimes->setChecked(false);
    }
}

void EntryDialog::onUseTimesToggled(bool enabled)
{
    m_ui->timeStart->setEnabled(enabled);
    m_ui->timeEnd->setEnabled(enabled);
    m_ui->checkDurationFromTimes->setEnabled(enabled);
    if (enabled && m_ui->checkDurationFromTimes->isChecked())
        updateDurationFromTimes();
}

void EntryDialog::onStartTimeChanged(const QTime &)
{
    updateDurationFromTimes();
}

void EntryDialog::onEndTimeChanged(const QTime &)
{
    updateDurationFromTimes();
}

void EntryDialog::updateDurationFromTimes()
{
    if (!m_ui->checkUseTimes->isChecked() || !m_ui->checkDurationFromTimes->isChecked())
        return;

    const QTime start = m_ui->timeStart->time();
    const QTime end = m_ui->timeEnd->time();
    if (!start.isValid() || !end.isValid() || end <= start)
        return;

    setMinutes(static_cast<int>(start.secsTo(end) / 60));
}

void EntryDialog::onDateChanged(const QDate &)
{
    updateDurationHint();
}

void EntryDialog::onAddQuarterHour() { addMinutes(15); }
void EntryDialog::onAddHalfHour()    { addMinutes(30); }
void EntryDialog::onAddOneHour()     { addMinutes(60); }

void EntryDialog::onSetHalfDay()
{
    setMinutes(m_settings.dailyTargetMinutes() / 2);
}

void EntryDialog::onSetFullDay()
{
    setMinutes(m_settings.dailyTargetMinutes());
}

void EntryDialog::onFillTheDay()
{
    const QDate date = m_ui->dateEdit->date();

    DayMeta meta;
    meta.date = date;
    const int target = m_settings.targetMinutesFor(date, meta);

    // What is already booked on that day, this entry's own former value aside:
    // editing a two-hour entry must not subtract those two hours twice.
    int alreadyLogged = m_suggestions.loggedMinutesByDate.value(date, 0);
    if (m_originalEntry.id >= 0 && m_originalEntry.date == date)
        alreadyLogged -= m_originalEntry.minutes;

    const int missing = target - qMax(0, alreadyLogged);
    if (missing <= 0) {
        m_ui->labelDurationHint->setText(QStringLiteral("this day already meets its target"));
        return;
    }
    setMinutes(missing);
}

void EntryDialog::applyIssueTemplate(const WorkEntry &source)
{
    m_ui->comboIssueKey->setCurrentText(source.issueKey);
    m_ui->lineSummary->setText(source.summary);
    m_ui->lineProject->setText(source.project);
    m_ui->comboIssueType->setCurrentText(source.issueType);
    m_ui->comboEpic->setCurrentText(source.epic);
    m_ui->comboSprint->setCurrentText(source.sprint);
    m_ui->comboComponent->setCurrentText(source.component);
    m_ui->comboFixVersion->setCurrentText(source.fixVersion);
    m_ui->comboPriority->setCurrentIndex(
        m_ui->comboPriority->findData(priorityToString(source.priority)));

    m_ui->lineIssueUrl->setText(source.issueUrl);
    m_ui->lineMergeRequest->setText(source.mergeRequest);
    m_ui->comboBranch->setCurrentText(source.branch);
    m_ui->lineTestsheetName->setText(source.testsheetName);
    m_ui->lineTestsheetUrl->setText(source.testsheetUrl);
    m_ui->lineTags->setText(source.tags);
}

void EntryDialog::onIssueKeyActivated(int index)
{
    const QString key = m_ui->comboIssueKey->itemData(index).toString();
    if (key.isEmpty())
        return;

    for (const WorkEntry &recent : m_suggestions.recentIssues) {
        if (recent.issueKey == key) {
            applyIssueTemplate(recent);
            return;
        }
    }
}

void EntryDialog::onFillFromRecentClicked()
{
    const QString key = m_ui->comboIssueKey->currentText().trimmed();
    if (key.isEmpty()) {
        m_ui->labelError->setText(
            QStringLiteral("Type or pick an issue key first, then its details can be reused."));
        return;
    }

    for (const WorkEntry &recent : m_suggestions.recentIssues) {
        if (recent.issueKey.compare(key, Qt::CaseInsensitive) == 0) {
            applyIssueTemplate(recent);
            m_ui->labelError->clear();
            return;
        }
    }

    m_ui->labelError->setText(
        QStringLiteral("No earlier entry for %1, so there is nothing to reuse yet.").arg(key));
}

void EntryDialog::openUrl(const QString &url)
{
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty()) {
        m_ui->labelError->setText(QStringLiteral("There is no address to open."));
        return;
    }
    m_ui->labelError->clear();
    QDesktopServices::openUrl(QUrl(trimmed));
}

void EntryDialog::onOpenIssueUrl()     { openUrl(m_ui->lineIssueUrl->text()); }
void EntryDialog::onOpenMergeRequest() { openUrl(m_ui->lineMergeRequest->text()); }
void EntryDialog::onOpenTestsheet()    { openUrl(m_ui->lineTestsheetUrl->text()); }

void EntryDialog::onAccepted()
{
    WorkEntry candidate = buildEntry();

    const int increment = m_settings.roundingIncrementMinutes();
    candidate.minutes = Duration::roundUpTo(candidate.minutes, increment);

    QString problem;
    if (!candidate.isValid(&problem)) {
        m_ui->labelError->setText(problem);
        return;
    }

    // A warning, not a refusal: long days happen, and the user knows better
    // than the application whether this one is real.
    if (candidate.minutes > 12 * 60) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            QStringLiteral("Unusually long entry"),
            QStringLiteral("%1 is a long time for a single entry. Save it anyway?")
                .arg(Duration::format(candidate.minutes)),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    m_resultEntry = candidate;
    accept();
}
