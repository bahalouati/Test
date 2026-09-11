#include "widgets/HoursBarChart.h"

#include "core/Duration.h"
#include "core/Settings.h"

#include <QHelpEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

namespace {

const int kAxisLabelHeight = 16;
const int kBarSpacing = 2;
const int kMinimumBarWidth = 6;

} // namespace

HoursBarChart::HoursBarChart(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
}

void HoursBarChart::setSettings(const Settings *settings)
{
    m_settings = settings;
    update();
}

void HoursBarChart::setDays(const QVector<DaySummary> &days)
{
    m_days = days;
    update();
}

void HoursBarChart::setSelectedDate(const QDate &date)
{
    if (m_selectedDate == date)
        return;
    m_selectedDate = date;
    update();
}

QRect HoursBarChart::plotRect() const
{
    return rect().adjusted(2, 4, -2, -kAxisLabelHeight);
}

int HoursBarChart::scaleMaximumMinutes() const
{
    // The scale is at least the target plus a quarter, so a normal day fills
    // most of the height and an overtime day visibly sticks out above the line.
    int maximum = m_settings ? m_settings->dailyTargetMinutes() : 8 * 60;
    maximum = maximum + maximum / 4;
    for (const DaySummary &day : m_days)
        maximum = qMax(maximum, day.totalMinutes);
    return qMax(60, maximum);
}

QRect HoursBarChart::slotRect(int index) const
{
    if (index < 0 || index >= m_days.size() || m_days.isEmpty())
        return QRect();

    const QRect plot = plotRect();
    if (plot.isEmpty())
        return QRect();

    const double slot = static_cast<double>(plot.width()) / static_cast<double>(m_days.size());
    const int x = plot.x() + static_cast<int>(index * slot);
    const int width = qMax(kMinimumBarWidth, static_cast<int>(slot) - kBarSpacing);

    return QRect(x, plot.y(), width, plot.height());
}

QRect HoursBarChart::barRect(int index) const
{
    const QRect slot = slotRect(index);
    if (slot.isEmpty())
        return QRect();

    const int maximum = scaleMaximumMinutes();
    const int barHeight = static_cast<int>(
        static_cast<double>(slot.height()) * m_days.at(index).totalMinutes / maximum);
    if (barHeight <= 0)
        return QRect();

    return QRect(slot.x(), slot.bottom() - barHeight, slot.width(), barHeight);
}

int HoursBarChart::barAt(const QPoint &position) const
{
    if (m_days.isEmpty())
        return -1;

    const QRect plot = plotRect();
    if (!plot.adjusted(0, 0, 0, kAxisLabelHeight).contains(position))
        return -1;

    const double slot = static_cast<double>(plot.width()) / static_cast<double>(m_days.size());
    if (slot <= 0.0)
        return -1;

    const int index = static_cast<int>((position.x() - plot.x()) / slot);
    return (index >= 0 && index < m_days.size()) ? index : -1;
}

void HoursBarChart::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().base());

    const QRect plot = plotRect();
    if (plot.isEmpty() || m_days.isEmpty())
        return;

    const int maximum = scaleMaximumMinutes();

    // The target line: the height every working day is aiming for.
    const int target = m_settings ? m_settings->dailyTargetMinutes() : 8 * 60;
    const int targetY = plot.bottom()
                        - static_cast<int>(static_cast<double>(plot.height()) * target / maximum);
    QPen targetPen(palette().color(QPalette::Mid));
    targetPen.setStyle(Qt::DashLine);
    painter.setPen(targetPen);
    painter.drawLine(plot.left(), targetY, plot.right(), targetY);
    painter.drawText(QRect(plot.left(), targetY - 14, plot.width(), 14),
                     Qt::AlignRight | Qt::AlignVCenter,
                     Duration::format(target, Duration::Format::HoursAndMinutes));

    const int threshold = m_settings ? m_settings->warningThresholdMinutes() : 6 * 60;
    const QDate today = QDate::currentDate();

    for (int index = 0; index < m_days.size(); ++index) {
        const DaySummary &day = m_days.at(index);
        const QRect slot = slotRect(index);
        const QRect bar = barRect(index);

        if (!bar.isEmpty()) {
            QColor color = m_settings ? m_settings->dayColor(day.state(today, threshold))
                                      : QColor(0xC0, 0xC0, 0xC0);
            painter.fillRect(bar, color);
            painter.setPen(color.darker(135));
            painter.drawRect(bar.adjusted(0, 0, -1, -1));
        }

        if (day.date == m_selectedDate) {
            QPen selectionPen(palette().color(QPalette::Highlight));
            selectionPen.setWidth(2);
            painter.setPen(selectionPen);
            painter.drawRect(slot);
        }

        // Label the Mondays, which is enough to find your way without crowding.
        // The column is used rather than the bar, so an empty Monday is still
        // labelled in the right place.
        if (day.date.dayOfWeek() == Qt::Monday) {
            painter.setPen(palette().color(QPalette::WindowText));
            const QRect labelRect(slot.x() - 4, plot.bottom() + 1, 30, kAxisLabelHeight - 2);
            painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter,
                             QString::number(day.date.day()));
        }
    }

    painter.setPen(palette().color(QPalette::Mid));
    painter.drawLine(plot.left(), plot.bottom(), plot.right(), plot.bottom());
}

void HoursBarChart::mousePressEvent(QMouseEvent *event)
{
    const int index = barAt(event->pos());
    if (index >= 0) {
        m_selectedDate = m_days.at(index).date;
        update();
        emit dateClicked(m_selectedDate);
    }
    QWidget::mousePressEvent(event);
}

bool HoursBarChart::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        QHelpEvent *helpEvent = static_cast<QHelpEvent *>(event);
        const int index = barAt(helpEvent->pos());
        if (index >= 0) {
            const DaySummary &day = m_days.at(index);
            const QString text = QStringLiteral("%1\n%2")
                                     .arg(QLocale().toString(day.date, QLocale::LongFormat),
                                          day.shortDescription());
            QToolTip::showText(helpEvent->globalPos(), text, this);
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QWidget::event(event);
}

QSize HoursBarChart::sizeHint() const
{
    return QSize(400, 110);
}

QSize HoursBarChart::minimumSizeHint() const
{
    return QSize(200, 70);
}
