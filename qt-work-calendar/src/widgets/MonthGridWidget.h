#ifndef WORKCALENDAR_MONTHGRIDWIDGET_H
#define WORKCALENDAR_MONTHGRIDWIDGET_H

#include "core/DaySummary.h"
#include "core/Duration.h"

#include <QDate>
#include <QHash>
#include <QRect>
#include <QVector>
#include <QWidget>

class Settings;

/*!
 * \brief The month view: one cell per day, coloured by how the day went.
 *
 * The widget draws everything itself rather than nesting a widget per day.
 * A month is at most 42 cells, and painting them is far cheaper - and far
 * easier to lay out - than building and deleting six rows of child widgets
 * every time the month changes.
 *
 * It owns no data. The window hands it the summaries it has just read from the
 * database through setDays(); the widget only decides how they look.
 *
 * Promote a QWidget to MonthGridWidget in Qt Designer to place one.
 */
class MonthGridWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MonthGridWidget(QWidget *parent = nullptr);

    /*! Appearance and colours are read from \a settings, which is not owned. */
    void setSettings(const Settings *settings);

    /*! Shows \a year / \a month (1-12). Days are cleared until setDays(). */
    void setMonth(int year, int month);

    int year() const;
    int month() const;

    /*! The first and last dates the grid currently covers, whole weeks included. */
    QDate firstVisibleDate() const;
    QDate lastVisibleDate() const;

    /*! The summaries to paint. Days outside the shown month are ignored. */
    void setDays(const QVector<DaySummary> &days);

    QDate selectedDate() const;
    void setSelectedDate(const QDate &date);

    /*! The day the user considers "now"; painted with a marker. */
    void setToday(const QDate &today);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    /*! The selection moved, by mouse or keyboard. */
    void dateSelected(const QDate &date);

    /*! A day was double clicked or Enter was pressed on it. */
    void dateActivated(const QDate &date);

    /*! The user asked for the context menu of \a date at \a globalPosition. */
    void dateContextMenuRequested(const QDate &date, const QPoint &globalPosition);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool event(QEvent *event) override;

private:
    /*! The weekdays shown as columns, left to right (Qt::Monday ... Qt::Sunday). */
    QVector<int> visibleWeekdays() const;

    /*! Recomputes the dates of every cell after the month or the columns change. */
    void rebuildCells();

    int columnCount() const;
    int rowCount() const;
    int headerHeight() const;

    /*! The rectangle of the cell at \a row / \a column, header excluded. */
    QRect cellRect(int row, int column) const;

    /*! The date under \a position, or an invalid date. */
    QDate dateAt(const QPoint &position) const;

    /*! The summary stored for \a date, or a default one. */
    DaySummary summaryFor(const QDate &date) const;

    /*! Moves the selection by \a days, following the visible columns. */
    void moveSelection(int days);

    void paintHeader(QPainter &painter);
    void paintCell(QPainter &painter, const QRect &rect, const QDate &date);
    void paintCellLines(QPainter &painter, const QRect &rect, const DaySummary &summary);

    /*! Colour of \a summary, taken from the settings. */
    QColor colorFor(const DaySummary &summary) const;

    /*! The multi-line text shown as the tooltip of \a date. */
    QString tooltipFor(const QDate &date) const;

    const Settings *m_settings = nullptr;

    int m_year = 2000;
    int m_month = 1;
    QDate m_selectedDate;
    QDate m_today;

    QVector<QDate> m_cellDates;             //!< row-major, columnCount() per row
    QHash<QDate, DaySummary> m_summaries;
};

#endif // WORKCALENDAR_MONTHGRIDWIDGET_H
