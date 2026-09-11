#include "timesheetsettingsdialog.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

TimesheetSettingsDialog::TimesheetSettingsDialog(const jira::TimesheetSettings &settings, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Timesheet settings"));
    setModal(true);

    m_sprintField = new QLineEdit(settings.sprintFieldId, this);
    m_sprintField->setPlaceholderText(QStringLiteral("customfield_10005"));
    m_sprintField->setToolTip(tr("The custom field holding the sprint. Leave it empty and every "
                                 "customfield_* is scanned for something sprint-shaped instead."));

    m_testSheetMarker = new QLineEdit(settings.testSheetMarker, this);
    m_testSheetMarker->setPlaceholderText(QStringLiteral("testsheet"));
    m_testSheetMarker->setToolTip(tr("An attachment counts as the test sheet when its file name "
                                     "contains this, ignoring case."));

    m_mergeRequestMarker = new QLineEdit(settings.mergeRequestMarker, this);
    m_mergeRequestMarker->setPlaceholderText(QStringLiteral("/merge_requests/"));
    m_mergeRequestMarker->setToolTip(tr("A remote link counts as the merge request when its URL "
                                        "contains this. Use /pull/ for GitHub. Empty skips the "
                                        "lookup entirely, which makes loading a month faster."));

    m_fullDay = new QDoubleSpinBox(this);
    m_fullDay->setRange(0.5, 24.0);
    m_fullDay->setSingleStep(0.5);
    m_fullDay->setDecimals(2);
    m_fullDay->setSuffix(tr(" h"));
    m_fullDay->setValue(settings.rules.fullDayHours);

    m_partialDay = new QDoubleSpinBox(this);
    m_partialDay->setRange(0.0, 24.0);
    m_partialDay->setSingleStep(0.5);
    m_partialDay->setDecimals(2);
    m_partialDay->setSuffix(tr(" h"));
    m_partialDay->setValue(settings.rules.partialDayHours);

    // The amber band has to sit below the green one or it can never show.
    connect(m_fullDay, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        m_partialDay->setMaximum(value);
    });
    m_partialDay->setMaximum(m_fullDay->value());

    auto *form = new QFormLayout;
    form->addRow(tr("Sprint field:"), m_sprintField);
    form->addRow(tr("Test sheet marker:"), m_testSheetMarker);
    form->addRow(tr("Merge request marker:"), m_mergeRequestMarker);
    form->addRow(tr("A full day is:"), m_fullDay);
    form->addRow(tr("Amber at or above:"), m_partialDay);

    auto *note = new QLabel(tr("A day at or above the full figure is green, at or above the amber "
                               "figure is amber, and anything below that is red. Days in the future "
                               "are grey and never count as missing."), this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: gray;"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addStretch();
    layout->addWidget(buttons);
    resize(520, sizeHint().height());
}

jira::TimesheetSettings TimesheetSettingsDialog::settings() const
{
    jira::TimesheetSettings result;
    result.sprintFieldId = m_sprintField->text().trimmed();
    result.testSheetMarker = m_testSheetMarker->text().trimmed();
    result.mergeRequestMarker = m_mergeRequestMarker->text().trimmed();
    result.rules.fullDayHours = m_fullDay->value();
    result.rules.partialDayHours = m_partialDay->value();
    return result;
}
