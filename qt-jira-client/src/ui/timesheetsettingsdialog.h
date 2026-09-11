#pragma once

#include "core/timesheet.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui { class TimesheetSettingsDialog; }
QT_END_NAMESPACE

// The conventions that differ per Jira instance: which custom field holds the
// sprint, what marks an attachment as the test sheet, what a merge request URL
// looks like, and how long a full day is. The layout lives in the .ui.
class TimesheetSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TimesheetSettingsDialog(const jira::TimesheetSettings &settings,
                                     QWidget *parent = nullptr);
    ~TimesheetSettingsDialog() override;

    jira::TimesheetSettings settings() const;

private:
    Ui::TimesheetSettingsDialog *ui;
};
