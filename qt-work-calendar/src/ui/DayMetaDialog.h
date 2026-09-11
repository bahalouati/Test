#ifndef WORKCALENDAR_DAYMETADIALOG_H
#define WORKCALENDAR_DAYMETADIALOG_H

#include "core/DayMeta.h"
#include "core/Settings.h"

#include <QDialog>

namespace Ui { class DayMetaDialog; }

/*!
 * \brief Declares what a day is: a normal working day, a holiday, a half day...
 *
 * This is how a public holiday stops being painted as missing time.
 */
class DayMetaDialog : public QDialog
{
    Q_OBJECT

public:
    DayMetaDialog(const DayMeta &meta, const Settings &settings, QWidget *parent = nullptr);
    ~DayMetaDialog() override;

    /*! The record as the user left it. */
    DayMeta dayMeta() const;

private slots:
    /*! Enables the target field and refreshes the hint next to it. */
    void onOverrideToggled(bool enabled);

    /*! Shows how the typed target was understood. */
    void onTargetTextChanged(const QString &text);

    /*! Shows the target the day type implies. */
    void onDayTypeChanged(int index);

    /*! Puts the day back to a plain working day with no note. */
    void onResetClicked();

private:
    /*! Refreshes the hint that spells out the resulting target. */
    void updateTargetHint();

    Ui::DayMetaDialog *m_ui;
    const Settings &m_settings;
    QDate m_date;
};

#endif // WORKCALENDAR_DAYMETADIALOG_H
