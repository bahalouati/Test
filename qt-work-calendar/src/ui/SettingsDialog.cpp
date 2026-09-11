#include "ui/SettingsDialog.h"

#include "ui_SettingsDialog.h"

#include "core/DaySummary.h"
#include "core/Duration.h"

#include <QColorDialog>
#include <QLabel>
#include <QPushButton>
#include <QSet>

SettingsDialog::SettingsDialog(Settings &settings, QWidget *parent)
    : QDialog(parent)
    , m_ui(new Ui::SettingsDialog)
    , m_settings(settings)
{
    m_ui->setupUi(this);

    const QVector<Activity> activities = allActivities();
    for (Activity activity : activities) {
        m_ui->comboDefaultActivity->addItem(activityDisplayName(activity),
                                            activityToString(activity));
    }

    const QVector<WorkLocation> locations = allWorkLocations();
    for (WorkLocation location : locations) {
        m_ui->comboDefaultLocation->addItem(workLocationDisplayName(location),
                                            workLocationToString(location));
    }

    m_ui->comboDurationFormat->addItem(QStringLiteral("7h 30m"),
                                       static_cast<int>(Duration::Format::HoursAndMinutes));
    m_ui->comboDurationFormat->addItem(QStringLiteral("7.50 h"),
                                       static_cast<int>(Duration::Format::Decimal));
    m_ui->comboDurationFormat->addItem(QStringLiteral("7:30"),
                                       static_cast<int>(Duration::Format::Clock));

    loadSettings();

    connect(m_ui->lineDailyTarget, &QLineEdit::textChanged,
            this, &SettingsDialog::onDailyTargetChanged);
    connect(m_ui->lineWarningThreshold, &QLineEdit::textChanged,
            this, &SettingsDialog::onWarningThresholdChanged);

    connect(m_ui->buttonColorComplete, &QPushButton::clicked,
            this, &SettingsDialog::onPickCompleteColor);
    connect(m_ui->buttonColorPartial, &QPushButton::clicked,
            this, &SettingsDialog::onPickPartialColor);
    connect(m_ui->buttonColorLow, &QPushButton::clicked,
            this, &SettingsDialog::onPickLowColor);
    connect(m_ui->buttonColorEmpty, &QPushButton::clicked,
            this, &SettingsDialog::onPickEmptyColor);
    connect(m_ui->buttonColorFuture, &QPushButton::clicked,
            this, &SettingsDialog::onPickFutureColor);
    connect(m_ui->buttonColorAbsence, &QPushButton::clicked,
            this, &SettingsDialog::onPickAbsenceColor);
    connect(m_ui->buttonColorNonWorking, &QPushButton::clicked,
            this, &SettingsDialog::onPickNonWorkingColor);
    connect(m_ui->buttonResetColors, &QPushButton::clicked,
            this, &SettingsDialog::onResetColors);

    connect(m_ui->buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccepted);
    connect(m_ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QPushButton *defaultsButton = m_ui->buttonBox->button(QDialogButtonBox::RestoreDefaults);
    if (defaultsButton)
        connect(defaultsButton, &QPushButton::clicked, this, &SettingsDialog::onRestoreDefaults);
}

SettingsDialog::~SettingsDialog()
{
    delete m_ui;
}

void SettingsDialog::loadSettings()
{
    m_ui->lineDailyTarget->setText(Duration::format(m_settings.dailyTargetMinutes()));
    m_ui->lineWarningThreshold->setText(Duration::format(m_settings.warningThresholdMinutes()));
    m_ui->lineUserName->setText(m_settings.userName());

    const QSet<int> workingDays = m_settings.workingDays();
    m_ui->checkMonday->setChecked(workingDays.contains(Qt::Monday));
    m_ui->checkTuesday->setChecked(workingDays.contains(Qt::Tuesday));
    m_ui->checkWednesday->setChecked(workingDays.contains(Qt::Wednesday));
    m_ui->checkThursday->setChecked(workingDays.contains(Qt::Thursday));
    m_ui->checkFriday->setChecked(workingDays.contains(Qt::Friday));
    m_ui->checkSaturday->setChecked(workingDays.contains(Qt::Saturday));
    m_ui->checkSunday->setChecked(workingDays.contains(Qt::Sunday));

    m_ui->comboDurationFormat->setCurrentIndex(m_ui->comboDurationFormat->findData(
        static_cast<int>(m_settings.durationFormat())));
    m_ui->spinMaxLines->setValue(m_settings.maxLinesPerDay());
    m_ui->checkShowWeekends->setChecked(m_settings.showWeekends());

    setColorButton(m_ui->buttonColorComplete, DaySummary::Complete,
                   m_settings.dayColor(DaySummary::Complete));
    setColorButton(m_ui->buttonColorPartial, DaySummary::Partial,
                   m_settings.dayColor(DaySummary::Partial));
    setColorButton(m_ui->buttonColorLow, DaySummary::Low,
                   m_settings.dayColor(DaySummary::Low));
    setColorButton(m_ui->buttonColorEmpty, DaySummary::Empty,
                   m_settings.dayColor(DaySummary::Empty));
    setColorButton(m_ui->buttonColorFuture, DaySummary::Future,
                   m_settings.dayColor(DaySummary::Future));
    setColorButton(m_ui->buttonColorAbsence, DaySummary::Absence,
                   m_settings.dayColor(DaySummary::Absence));
    setColorButton(m_ui->buttonColorNonWorking, DaySummary::NonWorking,
                   m_settings.dayColor(DaySummary::NonWorking));

    m_ui->comboDefaultActivity->setCurrentIndex(m_ui->comboDefaultActivity->findData(
        activityToString(m_settings.defaultActivity())));
    m_ui->comboDefaultLocation->setCurrentIndex(m_ui->comboDefaultLocation->findData(
        workLocationToString(m_settings.defaultLocation())));
    m_ui->timeDefaultStart->setTime(m_settings.defaultStartTime());
    m_ui->spinRounding->setValue(m_settings.roundingIncrementMinutes());
    m_ui->checkDefaultBillable->setChecked(m_settings.defaultBillable());

    m_ui->lineJiraUrl->setText(m_settings.jiraBaseUrl());
    m_ui->lineJiraUser->setText(m_settings.jiraUsername());
    m_ui->lineJiraToken->setText(m_settings.jiraToken());
    m_ui->lineSprintField->setText(m_settings.jiraSprintField());
    m_ui->lineTestsheetKeyword->setText(m_settings.jiraTestsheetKeyword());
    m_ui->lineMergeMarker->setText(m_settings.jiraMergeRequestMarker());

    onDailyTargetChanged(m_ui->lineDailyTarget->text());
    onWarningThresholdChanged(m_ui->lineWarningThreshold->text());
}

void SettingsDialog::updateDurationHint(QLabel *label, const QString &text)
{
    bool ok = false;
    const int minutes = Duration::parseMinutes(text, &ok);
    if (!ok || minutes <= 0) {
        label->setText(QStringLiteral("not understood"));
        return;
    }
    label->setText(QStringLiteral("%1 = %2 h")
                       .arg(Duration::format(minutes))
                       .arg(Duration::toHours(minutes), 0, 'f', 2));
}

void SettingsDialog::onDailyTargetChanged(const QString &text)
{
    updateDurationHint(m_ui->labelDailyTargetHint, text);
}

void SettingsDialog::onWarningThresholdChanged(const QString &text)
{
    updateDurationHint(m_ui->labelWarningHint, text);
}

void SettingsDialog::setColorButton(QPushButton *button, int state, const QColor &color)
{
    m_colors.insert(state, color);

    // A dark foreground on a light swatch and the other way round, so the hex
    // value stays readable whatever colour is chosen.
    const bool lightBackground = color.lightness() > 128;
    button->setStyleSheet(QStringLiteral("background-color: %1; color: %2;")
                              .arg(color.name(), lightBackground ? QStringLiteral("#202020")
                                                                 : QStringLiteral("#f0f0f0")));
    button->setText(color.name());
}

void SettingsDialog::pickColor(QPushButton *button, int state)
{
    const QColor chosen = QColorDialog::getColor(m_colors.value(state), this,
                                                 QStringLiteral("Day colour"));
    if (chosen.isValid())
        setColorButton(button, state, chosen);
}

void SettingsDialog::onPickCompleteColor()   { pickColor(m_ui->buttonColorComplete, DaySummary::Complete); }
void SettingsDialog::onPickPartialColor()    { pickColor(m_ui->buttonColorPartial, DaySummary::Partial); }
void SettingsDialog::onPickLowColor()        { pickColor(m_ui->buttonColorLow, DaySummary::Low); }
void SettingsDialog::onPickEmptyColor()      { pickColor(m_ui->buttonColorEmpty, DaySummary::Empty); }
void SettingsDialog::onPickFutureColor()     { pickColor(m_ui->buttonColorFuture, DaySummary::Future); }
void SettingsDialog::onPickAbsenceColor()    { pickColor(m_ui->buttonColorAbsence, DaySummary::Absence); }
void SettingsDialog::onPickNonWorkingColor() { pickColor(m_ui->buttonColorNonWorking, DaySummary::NonWorking); }

void SettingsDialog::onResetColors()
{
    setColorButton(m_ui->buttonColorComplete, DaySummary::Complete,
                   m_settings.defaultDayColor(DaySummary::Complete));
    setColorButton(m_ui->buttonColorPartial, DaySummary::Partial,
                   m_settings.defaultDayColor(DaySummary::Partial));
    setColorButton(m_ui->buttonColorLow, DaySummary::Low,
                   m_settings.defaultDayColor(DaySummary::Low));
    setColorButton(m_ui->buttonColorEmpty, DaySummary::Empty,
                   m_settings.defaultDayColor(DaySummary::Empty));
    setColorButton(m_ui->buttonColorFuture, DaySummary::Future,
                   m_settings.defaultDayColor(DaySummary::Future));
    setColorButton(m_ui->buttonColorAbsence, DaySummary::Absence,
                   m_settings.defaultDayColor(DaySummary::Absence));
    setColorButton(m_ui->buttonColorNonWorking, DaySummary::NonWorking,
                   m_settings.defaultDayColor(DaySummary::NonWorking));
}

void SettingsDialog::onRestoreDefaults()
{
    m_ui->lineDailyTarget->setText(QStringLiteral("8h"));
    m_ui->lineWarningThreshold->setText(QStringLiteral("6h"));

    m_ui->checkMonday->setChecked(true);
    m_ui->checkTuesday->setChecked(true);
    m_ui->checkWednesday->setChecked(true);
    m_ui->checkThursday->setChecked(true);
    m_ui->checkFriday->setChecked(true);
    m_ui->checkSaturday->setChecked(false);
    m_ui->checkSunday->setChecked(false);

    m_ui->comboDurationFormat->setCurrentIndex(m_ui->comboDurationFormat->findData(
        static_cast<int>(Duration::Format::HoursAndMinutes)));
    m_ui->spinMaxLines->setValue(5);
    m_ui->checkShowWeekends->setChecked(false);
    m_ui->spinRounding->setValue(0);
    m_ui->checkDefaultBillable->setChecked(true);
    m_ui->timeDefaultStart->setTime(QTime(9, 0));

    onResetColors();
}

void SettingsDialog::onAccepted()
{
    bool ok = false;
    const int dailyTarget = Duration::parseMinutes(m_ui->lineDailyTarget->text(), &ok);
    if (ok && dailyTarget > 0)
        m_settings.setDailyTargetMinutes(dailyTarget);

    const int warning = Duration::parseMinutes(m_ui->lineWarningThreshold->text(), &ok);
    if (ok)
        m_settings.setWarningThresholdMinutes(warning);

    m_settings.setUserName(m_ui->lineUserName->text());

    QSet<int> workingDays;
    if (m_ui->checkMonday->isChecked())    workingDays.insert(Qt::Monday);
    if (m_ui->checkTuesday->isChecked())   workingDays.insert(Qt::Tuesday);
    if (m_ui->checkWednesday->isChecked()) workingDays.insert(Qt::Wednesday);
    if (m_ui->checkThursday->isChecked())  workingDays.insert(Qt::Thursday);
    if (m_ui->checkFriday->isChecked())    workingDays.insert(Qt::Friday);
    if (m_ui->checkSaturday->isChecked())  workingDays.insert(Qt::Saturday);
    if (m_ui->checkSunday->isChecked())    workingDays.insert(Qt::Sunday);
    m_settings.setWorkingDays(workingDays);

    m_settings.setDurationFormat(
        static_cast<Duration::Format>(m_ui->comboDurationFormat->currentData().toInt()));
    m_settings.setMaxLinesPerDay(m_ui->spinMaxLines->value());
    m_settings.setShowWeekends(m_ui->checkShowWeekends->isChecked());

    QMap<int, QColor>::const_iterator colorIt = m_colors.constBegin();
    for (; colorIt != m_colors.constEnd(); ++colorIt)
        m_settings.setDayColor(colorIt.key(), colorIt.value());

    m_settings.setDefaultActivity(
        activityFromString(m_ui->comboDefaultActivity->currentData().toString()));
    m_settings.setDefaultLocation(
        workLocationFromString(m_ui->comboDefaultLocation->currentData().toString()));
    m_settings.setDefaultStartTime(m_ui->timeDefaultStart->time());
    m_settings.setRoundingIncrementMinutes(m_ui->spinRounding->value());
    m_settings.setDefaultBillable(m_ui->checkDefaultBillable->isChecked());

    m_settings.setJiraBaseUrl(m_ui->lineJiraUrl->text());
    m_settings.setJiraUsername(m_ui->lineJiraUser->text());
    m_settings.setJiraToken(m_ui->lineJiraToken->text());
    m_settings.setJiraSprintField(m_ui->lineSprintField->text());
    m_settings.setJiraTestsheetKeyword(m_ui->lineTestsheetKeyword->text());
    m_settings.setJiraMergeRequestMarker(m_ui->lineMergeMarker->text());

    m_settings.sync();
    accept();
}
