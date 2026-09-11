#ifndef WORKCALENDAR_SETTINGSDIALOG_H
#define WORKCALENDAR_SETTINGSDIALOG_H

#include "core/Settings.h"

#include <QColor>
#include <QDialog>
#include <QMap>

class QPushButton;

namespace Ui { class SettingsDialog; }

/*!
 * \brief Edits the preferences.
 *
 * Nothing is written until the dialog is accepted, so Cancel really cancels.
 * The colour buttons show the colour they carry as their own background, which
 * avoids a swatch widget being built for each of them.
 */
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    SettingsDialog(Settings &settings, QWidget *parent = nullptr);
    ~SettingsDialog() override;

private slots:
    void onDailyTargetChanged(const QString &text);
    void onWarningThresholdChanged(const QString &text);

    void onPickCompleteColor();
    void onPickPartialColor();
    void onPickLowColor();
    void onPickEmptyColor();
    void onPickFutureColor();
    void onPickAbsenceColor();
    void onPickNonWorkingColor();
    void onResetColors();

    void onRestoreDefaults();

    /*! Writes every value back into the settings and closes. */
    void onAccepted();

private:
    /*! Fills the widgets from the settings. */
    void loadSettings();

    /*! Paints \a button with \a color and remembers the choice. */
    void setColorButton(QPushButton *button, int state, const QColor &color);

    /*! Asks for a colour for \a state and applies it to \a button. */
    void pickColor(QPushButton *button, int state);

    /*! Updates the hint that spells out a typed duration. */
    void updateDurationHint(class QLabel *label, const QString &text);

    Ui::SettingsDialog *m_ui;
    Settings &m_settings;

    /*! The colours as edited, applied only when the dialog is accepted. */
    QMap<int, QColor> m_colors;
};

#endif // WORKCALENDAR_SETTINGSDIALOG_H
