#include "ui/AboutDialog.h"

#include "ui_AboutDialog.h"

#include <QCoreApplication>

AboutDialog::AboutDialog(const QString &databasePath, QWidget *parent)
    : QDialog(parent)
    , m_ui(new Ui::AboutDialog)
{
    m_ui->setupUi(this);

    m_ui->labelVersion->setText(
        QStringLiteral("Version %1, built against Qt %2")
            .arg(QCoreApplication::applicationVersion(), QStringLiteral(QT_VERSION_STR)));

    m_ui->textAbout->setHtml(QStringLiteral(
        "<p>A work log that lives on your own machine. Log what you did, see the month "
        "at a glance, and hand out the spreadsheet when someone asks for one.</p>"
        "<ul>"
        "<li><b>Calendar</b> - every day coloured against your daily target.</li>"
        "<li><b>Entries</b> - every field you logged, sortable, filterable, and with the "
        "columns you choose.</li>"
        "<li><b>Reports</b> - totals, balance and breakdowns by project, activity, sprint "
        "or issue.</li>"
        "<li><b>Export</b> - a three-sheet Excel workbook or a CSV file.</li>"
        "<li><b>Jira import</b> - your own worklogs for a period, refreshed rather than "
        "duplicated when you run it again.</li>"
        "</ul>"
        "<p>Keyboard: <b>Ctrl+N</b> new entry, <b>Ctrl+E</b> edit, <b>Ctrl+D</b> duplicate, "
        "<b>Ctrl+T</b> day type, <b>Ctrl+Shift+S</b> start the timer, "
        "<b>Ctrl+Shift+C</b> copy the stand-up note. In the calendar, the arrow keys move "
        "one day, Page Up and Page Down move one month, and Enter opens the selected day.</p>"));

    m_ui->labelDatabase->setText(QStringLiteral("Database: %1").arg(databasePath));

    connect(m_ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::accept);
}

AboutDialog::~AboutDialog()
{
    delete m_ui;
}
