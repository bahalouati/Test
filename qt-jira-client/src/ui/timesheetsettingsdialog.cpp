#include "timesheetsettingsdialog.h"
#include "ui_timesheetsettingsdialog.h"

#include <QDoubleSpinBox>
#include <QLineEdit>

TimesheetSettingsDialog::TimesheetSettingsDialog(const jira::TimesheetSettings &settings,
                                                 QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::TimesheetSettingsDialog)
{
    ui->setupUi(this);

    ui->sprintField->setText(settings.sprintFieldId);
    ui->testSheetMarker->setText(settings.testSheetMarker);
    ui->mergeRequestMarker->setText(settings.mergeRequestMarker);
    ui->fullDay->setValue(settings.rules.fullDayHours);
    ui->partialDay->setValue(settings.rules.partialDayHours);

    // The amber band has to sit below the green one or it can never show.
    connect(ui->fullDay, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        ui->partialDay->setMaximum(value);
    });
    ui->partialDay->setMaximum(ui->fullDay->value());
}

TimesheetSettingsDialog::~TimesheetSettingsDialog()
{
    delete ui;
}

jira::TimesheetSettings TimesheetSettingsDialog::settings() const
{
    jira::TimesheetSettings result;
    result.sprintFieldId = ui->sprintField->text().trimmed();
    result.testSheetMarker = ui->testSheetMarker->text().trimmed();
    result.mergeRequestMarker = ui->mergeRequestMarker->text().trimmed();
    result.rules.fullDayHours = ui->fullDay->value();
    result.rules.partialDayHours = ui->partialDay->value();
    return result;
}
