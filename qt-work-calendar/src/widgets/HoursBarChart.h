#ifndef WORKCALENDAR_HOURSBARCHART_H
#define WORKCALENDAR_HOURSBARCHART_H

#include "core/DaySummary.h"

#include <QDate>
#include <QVector>
#include <QWidget>

class Settings;

/*!
 * \brief A bar per day of the shown period, with the daily target as a line.
 *
 * It answers one question at a glance - "where did the hours go this month?" -
 * and it clicks through to the day, which makes it a navigation aid as much as
 * a chart.
 *
 * Promote a QWidget to HoursBarChart in Qt Designer to place one.
 */
class HoursBarChart : public QWidget
{
    Q_OBJECT

public:
    explicit HoursBarChart(QWidget *parent = nullptr);

    /*! Colours are read from \a settings, which is not owned. */
    void setSettings(const Settings *settings);

    /*! The days to plot, in calendar order. */
    void setDays(const QVector<DaySummary> &days);

    /*! Marks one bar as selected. */
    void setSelectedDate(const QDate &date);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    /*! A bar was clicked. */
    void dateClicked(const QDate &date);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool event(QEvent *event) override;

private:
    /*! The plotting area, excluding the axis labels. */
    QRect plotRect() const;

    /*!
     * \brief The full-height column of the day at \a index.
     *
     * Kept separate from barRect() because a day with no hours has a bar of
     * zero height, and its column is still needed to place the date label and
     * the selection marker.
     */
    QRect slotRect(int index) const;

    /*! The bar rectangle of the day at \a index; empty when nothing was logged. */
    QRect barRect(int index) const;

    /*! Index of the bar under \a position, or -1. */
    int barAt(const QPoint &position) const;

    /*! The tallest value the vertical axis must show, in minutes. */
    int scaleMaximumMinutes() const;

    const Settings *m_settings = nullptr;
    QVector<DaySummary> m_days;
    QDate m_selectedDate;
};

#endif // WORKCALENDAR_HOURSBARCHART_H
