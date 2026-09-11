#ifndef WORKCALENDAR_ABOUTDIALOG_H
#define WORKCALENDAR_ABOUTDIALOG_H

#include <QDialog>
#include <QString>

namespace Ui { class AboutDialog; }

/*! Shows the version, what the application does, and where its data lives. */
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    /*! \a databasePath is shown so the user can find and back up their data. */
    explicit AboutDialog(const QString &databasePath, QWidget *parent = nullptr);
    ~AboutDialog() override;

private:
    Ui::AboutDialog *m_ui;
};

#endif // WORKCALENDAR_ABOUTDIALOG_H
