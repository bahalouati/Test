#pragma once

#include "core/timesheet.h"

#include <QDialog>

class QDoubleSpinBox;
class QLineEdit;

// The conventions that differ per Jira instance: which custom field holds the
// sprint, what marks an attachment as the test sheet, what a merge request URL
// looks like, and how long a full day is.
class TimesheetSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TimesheetSettingsDialog(const jira::TimesheetSettings &settings, QWidget *parent = nullptr);

    jira::TimesheetSettings settings() const;

private:
    QLineEdit *m_sprintField;
    QLineEdit *m_testSheetMarker;
    QLineEdit *m_mergeRequestMarker;
    QDoubleSpinBox *m_fullDay;
    QDoubleSpinBox *m_partialDay;
};
