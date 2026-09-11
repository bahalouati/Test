#include "ui/ColumnsDialog.h"

#include "ui_ColumnsDialog.h"

#include <QListWidgetItem>

namespace {

/*! The role storing which WorkEntry::Field a row stands for. */
const int kFieldRole = Qt::UserRole + 1;

} // namespace

ColumnsDialog::ColumnsDialog(const QVector<WorkEntry::Field> &visibleColumns, QWidget *parent)
    : QDialog(parent)
    , m_ui(new Ui::ColumnsDialog)
{
    m_ui->setupUi(this);

    populate(visibleColumns);

    connect(m_ui->buttonSelectAll, &QPushButton::clicked,
            this, &ColumnsDialog::onSelectAllClicked);
    connect(m_ui->buttonSelectNone, &QPushButton::clicked,
            this, &ColumnsDialog::onSelectNoneClicked);
    connect(m_ui->buttonRestoreDefaults, &QPushButton::clicked,
            this, &ColumnsDialog::onRestoreDefaultsClicked);
    connect(m_ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

ColumnsDialog::~ColumnsDialog()
{
    delete m_ui;
}

void ColumnsDialog::populate(const QVector<WorkEntry::Field> &visibleColumns)
{
    m_ui->listColumns->clear();

    // The visible fields come first in their current order, then the rest, so
    // the list reads like the table it configures.
    QVector<WorkEntry::Field> ordered = visibleColumns;
    const QVector<WorkEntry::Field> allFields = WorkEntry::allFields();
    for (WorkEntry::Field field : allFields) {
        if (!ordered.contains(field))
            ordered.append(field);
    }

    for (WorkEntry::Field field : ordered) {
        QListWidgetItem *item = new QListWidgetItem(WorkEntry::fieldHeader(field),
                                                    m_ui->listColumns);
        item->setData(kFieldRole, static_cast<int>(field));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(visibleColumns.contains(field) ? Qt::Checked : Qt::Unchecked);
    }
}

QVector<WorkEntry::Field> ColumnsDialog::selectedColumns() const
{
    QVector<WorkEntry::Field> columns;
    for (int row = 0; row < m_ui->listColumns->count(); ++row) {
        const QListWidgetItem *item = m_ui->listColumns->item(row);
        if (item->checkState() != Qt::Checked)
            continue;
        columns.append(static_cast<WorkEntry::Field>(item->data(kFieldRole).toInt()));
    }

    // Never hand back an empty table; a column-less view would look broken.
    if (columns.isEmpty())
        columns = WorkEntry::defaultVisibleFields();
    return columns;
}

void ColumnsDialog::setAllChecked(bool checked)
{
    for (int row = 0; row < m_ui->listColumns->count(); ++row)
        m_ui->listColumns->item(row)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
}

void ColumnsDialog::onSelectAllClicked()
{
    setAllChecked(true);
}

void ColumnsDialog::onSelectNoneClicked()
{
    setAllChecked(false);
}

void ColumnsDialog::onRestoreDefaultsClicked()
{
    populate(WorkEntry::defaultVisibleFields());
}
