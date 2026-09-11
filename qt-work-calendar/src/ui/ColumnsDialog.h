#ifndef WORKCALENDAR_COLUMNSDIALOG_H
#define WORKCALENDAR_COLUMNSDIALOG_H

#include "core/WorkEntry.h"

#include <QDialog>
#include <QVector>

namespace Ui { class ColumnsDialog; }

/*!
 * \brief Chooses which entry fields the table shows, and in which order.
 *
 * The list holds one checkable item per field. Items - unlike widgets - are
 * lightweight data the list view draws itself, so the dialog stays a fixed
 * form with no widgets built at run time.
 */
class ColumnsDialog : public QDialog
{
    Q_OBJECT

public:
    /*! \a visibleColumns are ticked and listed first, in their current order. */
    explicit ColumnsDialog(const QVector<WorkEntry::Field> &visibleColumns,
                           QWidget *parent = nullptr);
    ~ColumnsDialog() override;

    /*! The ticked fields, in the order the user arranged them. */
    QVector<WorkEntry::Field> selectedColumns() const;

private slots:
    void onSelectAllClicked();
    void onSelectNoneClicked();
    void onRestoreDefaultsClicked();

private:
    /*! Fills the list, ticking everything in \a visibleColumns. */
    void populate(const QVector<WorkEntry::Field> &visibleColumns);

    /*! Ticks or unticks every row. */
    void setAllChecked(bool checked);

    Ui::ColumnsDialog *m_ui;
};

#endif // WORKCALENDAR_COLUMNSDIALOG_H
