#include "widgets/MonthGridWidget.h"

#include "core/Settings.h"

#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLocale>
#include <QStringList>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

namespace {

const int kCellPadding = 6;
const int kMinimumCellWidth = 120;
const int kMinimumCellHeight = 84;
const int kProgressBarHeight = 4;

/*! A colour that stays readable on \a background. */
QColor readableTextColor(const QColor &background)
{
    // Rec. 601 luma: close enough to perceived brightness for picking black or
    // white, and it needs no colour-space conversion.
    const double luma = 0.299 * background.red()
                      + 0.587 * background.green()
                      + 0.114 * background.blue();
    return luma > 140.0 ? QColor(0x20, 0x20, 0x20) : QColor(0xF5, 0xF5, 0xF5);
}

/*! Shortens \a text to fit \a width, appending an ellipsis when it must. */
QString elide(const QFontMetrics &metrics, const QString &text, int width)
{
    return metrics.elidedText(text, Qt::ElideRight, width);
}

} // namespace

MonthGridWidget::MonthGridWidget(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);

    const QDate today = QDate::currentDate();
    m_today = today;
    m_year = today.year();
    m_month = today.month();
    m_selectedDate = today;
    rebuildCells();
}

void MonthGridWidget::setSettings(const Settings *settings)
{
    m_settings = settings;
    rebuildCells();
    update();
}

void MonthGridWidget::setMonth(int year, int month)
{
    if (month < 1 || month > 12)
        return;
    if (m_year == year && m_month == month)
        return;

    m_year = year;
    m_month = month;
    m_summaries.clear();
    rebuildCells();

    // Keep the selection inside the shown month so the day panel never shows a
    // day the user cannot see.
    if (m_selectedDate.year() != year || m_selectedDate.month() != month) {
        const QDate first(year, month, 1);
        const bool todayIsHere = m_today.year() == year && m_today.month() == month;
        m_selectedDate = todayIsHere ? m_today : first;
        emit dateSelected(m_selectedDate);
    }
    update();
}

int MonthGridWidget::year() const { return m_year; }
int MonthGridWidget::month() const { return m_month; }

QDate MonthGridWidget::firstVisibleDate() const
{
    return m_cellDates.isEmpty() ? QDate() : m_cellDates.first();
}

QDate MonthGridWidget::lastVisibleDate() const
{
    return m_cellDates.isEmpty() ? QDate() : m_cellDates.last();
}

void MonthGridWidget::setDays(const QVector<DaySummary> &days)
{
    m_summaries.clear();
    for (const DaySummary &day : days)
        m_summaries.insert(day.date, day);
    update();
}

QDate MonthGridWidget::selectedDate() const
{
    return m_selectedDate;
}

void MonthGridWidget::setSelectedDate(const QDate &date)
{
    if (!date.isValid() || m_selectedDate == date)
        return;

    m_selectedDate = date;
    if (date.year() != m_year || date.month() != m_month)
        setMonth(date.year(), date.month());
    update();
    emit dateSelected(m_selectedDate);
}

void MonthGridWidget::setToday(const QDate &today)
{
    m_today = today;
    update();
}

QVector<int> MonthGridWidget::visibleWeekdays() const
{
    QVector<int> weekdays;
    const bool includeWeekend = m_settings ? m_settings->showWeekends() : false;
    const int lastDay = includeWeekend ? Qt::Sunday : Qt::Friday;
    for (int day = Qt::Monday; day <= lastDay; ++day)
        weekdays.append(day);
    return weekdays;
}

int MonthGridWidget::columnCount() const
{
    return visibleWeekdays().size();
}

int MonthGridWidget::rowCount() const
{
    const int columns = columnCount();
    if (columns <= 0)
        return 0;
    return m_cellDates.size() / columns;
}

void MonthGridWidget::rebuildCells()
{
    m_cellDates.clear();

    const QVector<int> weekdays = visibleWeekdays();
    if (weekdays.isEmpty())
        return;

    const QDate firstOfMonth(m_year, m_month, 1);
    if (!firstOfMonth.isValid())
        return;

    // Start on the Monday of the week containing the 1st, and run whole weeks
    // until the month is covered. Hidden weekend days simply get no column.
    QDate weekStart = firstOfMonth.addDays(-(firstOfMonth.dayOfWeek() - Qt::Monday));
    const QDate lastOfMonth = firstOfMonth.addMonths(1).addDays(-1);

    while (weekStart <= lastOfMonth) {
        for (int weekday : weekdays)
            m_cellDates.append(weekStart.addDays(weekday - Qt::Monday));
        weekStart = weekStart.addDays(7);
    }
}

int MonthGridWidget::headerHeight() const
{
    return fontMetrics().height() + 10;
}

QRect MonthGridWidget::cellRect(int row, int column) const
{
    const int columns = columnCount();
    const int rows = rowCount();
    if (columns <= 0 || rows <= 0)
        return QRect();

    const int top = headerHeight();
    const int availableHeight = height() - top;
    const int cellWidth = width() / columns;
    const int cellHeight = availableHeight / rows;

    // The last row and column absorb the rounding remainder so the grid always
    // fills the widget exactly.
    const int x = column * cellWidth;
    const int y = top + row * cellHeight;
    const int w = (column == columns - 1) ? width() - x : cellWidth;
    const int h = (row == rows - 1) ? height() - y : cellHeight;

    return QRect(x, y, w, h);
}

QDate MonthGridWidget::dateAt(const QPoint &position) const
{
    for (int row = 0; row < rowCount(); ++row) {
        for (int column = 0; column < columnCount(); ++column) {
            if (cellRect(row, column).contains(position))
                return m_cellDates.value(row * columnCount() + column);
        }
    }
    return QDate();
}

DaySummary MonthGridWidget::summaryFor(const QDate &date) const
{
    DaySummary summary = m_summaries.value(date);
    if (!summary.date.isValid()) {
        summary.date = date;
        summary.meta.date = date;
    }
    return summary;
}

QColor MonthGridWidget::colorFor(const DaySummary &summary) const
{
    const int threshold = m_settings ? m_settings->warningThresholdMinutes() : 6 * 60;
    const DaySummary::State state = summary.state(m_today, threshold);
    if (m_settings)
        return m_settings->dayColor(state);

    // Without settings (for instance inside Qt Designer) fall back to plain grey.
    return QColor(0xF0, 0xF0, 0xF0);
}

void MonthGridWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(rect(), palette().base());

    paintHeader(painter);

    for (int row = 0; row < rowCount(); ++row) {
        for (int column = 0; column < columnCount(); ++column) {
            const QDate date = m_cellDates.value(row * columnCount() + column);
            paintCell(painter, cellRect(row, column), date);
        }
    }
}

void MonthGridWidget::paintHeader(QPainter &painter)
{
    const QVector<int> weekdays = visibleWeekdays();
    const QRect headerRect(0, 0, width(), headerHeight());
    painter.fillRect(headerRect, palette().window());

    QFont headerFont = font();
    headerFont.setBold(true);
    painter.setFont(headerFont);
    painter.setPen(palette().color(QPalette::WindowText));

    const QLocale locale;
    for (int column = 0; column < weekdays.size(); ++column) {
        const QRect cell = cellRect(0, column);
        const QRect labelRect(cell.x(), 0, cell.width(), headerHeight());
        painter.drawText(labelRect, Qt::AlignCenter, locale.dayName(weekdays.at(column)));
    }

    painter.setPen(palette().color(QPalette::Mid));
    painter.drawLine(0, headerHeight() - 1, width(), headerHeight() - 1);
    painter.setFont(font());
}

void MonthGridWidget::paintCell(QPainter &painter, const QRect &rect, const QDate &date)
{
    if (!date.isValid() || rect.isEmpty())
        return;

    const bool insideMonth = date.month() == m_month && date.year() == m_year;
    const DaySummary summary = summaryFor(date);

    QColor background = colorFor(summary);
    if (!insideMonth) {
        // Leading and trailing days belong to the neighbouring months: keep
        // them recognisable but clearly out of the way.
        background = background.lighter(112);
        background.setAlpha(90);
    }

    const QRect body = rect.adjusted(1, 1, -1, -1);
    painter.fillRect(body, background);

    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(body.adjusted(0, 0, -1, -1));

    const QColor textColor = insideMonth ? readableTextColor(background)
                                         : palette().color(QPalette::Disabled, QPalette::Text);
    const QRect content = body.adjusted(kCellPadding, kCellPadding, -kCellPadding, -kCellPadding);
    if (content.isEmpty())
        return;

    // ---- first line: day number on the left, totals on the right
    QFont dayFont = font();
    dayFont.setBold(true);
    dayFont.setPointSize(font().pointSize() + 2);
    painter.setFont(dayFont);
    painter.setPen(textColor);

    const QFontMetrics dayMetrics(dayFont);
    const int titleHeight = dayMetrics.height();
    const QRect titleRect(content.x(), content.y(), content.width(), titleHeight);
    painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, QString::number(date.day()));

    if (summary.totalMinutes > 0 || summary.targetMinutes > 0) {
        const Duration::Format format = m_settings ? m_settings->durationFormat()
                                                   : Duration::Format::HoursAndMinutes;
        QString totalText = Duration::format(summary.totalMinutes, format);
        if (summary.targetMinutes > 0 && summary.totalMinutes != summary.targetMinutes)
            totalText += QStringLiteral(" / %1").arg(Duration::format(summary.targetMinutes, format));
        painter.drawText(titleRect, Qt::AlignRight | Qt::AlignVCenter,
                         elide(dayMetrics, totalText, content.width() * 3 / 4));
    }

    painter.setFont(font());

    int nextY = content.y() + titleHeight + 3;

    // ---- day type and note, when the day is not an ordinary one
    if (summary.meta.type != DayType::Workday || !summary.meta.note.trimmed().isEmpty()) {
        QString badge = summary.meta.type != DayType::Workday
                            ? dayTypeDisplayName(summary.meta.type)
                            : QString();
        if (!summary.meta.note.trimmed().isEmpty()) {
            if (!badge.isEmpty())
                badge += QStringLiteral(" - ");
            badge += summary.meta.note.trimmed();
        }

        QFont badgeFont = font();
        badgeFont.setItalic(true);
        painter.setFont(badgeFont);
        painter.setPen(textColor);
        const QFontMetrics badgeMetrics(badgeFont);
        const QRect badgeRect(content.x(), nextY, content.width(), badgeMetrics.height());
        painter.drawText(badgeRect, Qt::AlignLeft | Qt::AlignVCenter,
                         elide(badgeMetrics, badge, content.width()));
        nextY += badgeMetrics.height() + 2;
        painter.setFont(font());
    }

    // ---- progress towards the target
    if (summary.targetMinutes > 0) {
        const QRect track(content.x(), nextY, content.width(), kProgressBarHeight);
        QColor trackColor = textColor;
        trackColor.setAlpha(45);
        painter.fillRect(track, trackColor);

        const int filledWidth = static_cast<int>(track.width() * summary.completion());
        if (filledWidth > 0) {
            QColor fillColor = textColor;
            fillColor.setAlpha(140);
            painter.fillRect(QRect(track.x(), track.y(), filledWidth, track.height()), fillColor);
        }
        nextY += kProgressBarHeight + 4;
    }

    // ---- the entries themselves
    const QRect linesRect(content.x(), nextY, content.width(), content.bottom() - nextY);
    if (linesRect.height() > 0) {
        painter.setPen(textColor);
        paintCellLines(painter, linesRect, summary);
    }

    // ---- selection and today markers, drawn last so nothing covers them
    if (date == m_today) {
        QPen todayPen(palette().color(QPalette::Highlight).darker(130));
        todayPen.setWidth(2);
        todayPen.setStyle(Qt::DashLine);
        painter.setPen(todayPen);
        painter.drawRect(body.adjusted(1, 1, -2, -2));
    }
    if (date == m_selectedDate) {
        QPen selectionPen(palette().color(QPalette::Highlight));
        selectionPen.setWidth(3);
        painter.setPen(selectionPen);
        painter.drawRect(body.adjusted(1, 1, -2, -2));
    }
}

void MonthGridWidget::paintCellLines(QPainter &painter, const QRect &rect, const DaySummary &summary)
{
    if (summary.lines.isEmpty())
        return;

    const QFontMetrics metrics = painter.fontMetrics();
    const int lineHeight = metrics.height();
    const int maxLinesThatFit = rect.height() / lineHeight;
    const int configuredMax = m_settings ? m_settings->maxLinesPerDay() : 5;
    const int maxLines = qMin(maxLinesThatFit, configuredMax);
    if (maxLines <= 0)
        return;

    const Duration::Format format = m_settings ? m_settings->durationFormat()
                                               : Duration::Format::HoursAndMinutes;

    // When not everything fits, the last usable line becomes "+n more" so the
    // cell never lies about how much it is showing.
    const bool needsMoreLine = summary.lines.size() > maxLines;
    const int shownLines = needsMoreLine ? maxLines - 1 : summary.lines.size();

    int y = rect.y();
    for (int i = 0; i < shownLines; ++i) {
        const DayLine &line = summary.lines.at(i);
        const QString duration = Duration::format(line.minutes, format);
        const int durationWidth = metrics.horizontalAdvance(duration) + 6;

        const QRect lineRect(rect.x(), y, rect.width(), lineHeight);
        painter.drawText(QRect(lineRect.x(), y, lineRect.width() - durationWidth, lineHeight),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         elide(metrics, line.label, lineRect.width() - durationWidth));
        painter.drawText(lineRect, Qt::AlignRight | Qt::AlignVCenter, duration);
        y += lineHeight;
    }

    if (needsMoreLine && shownLines >= 0) {
        const int hidden = summary.lines.size() - shownLines;
        QFont moreFont = painter.font();
        moreFont.setItalic(true);
        painter.setFont(moreFont);
        painter.drawText(QRect(rect.x(), y, rect.width(), lineHeight),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("+%1 more").arg(hidden));
    }
}

void MonthGridWidget::mousePressEvent(QMouseEvent *event)
{
    const QDate date = dateAt(event->pos());
    if (date.isValid() && date != m_selectedDate) {
        m_selectedDate = date;
        update();
        emit dateSelected(date);
    }
    QWidget::mousePressEvent(event);
}

void MonthGridWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    const QDate date = dateAt(event->pos());
    if (date.isValid()) {
        m_selectedDate = date;
        update();
        emit dateSelected(date);
        emit dateActivated(date);
    }
    QWidget::mouseDoubleClickEvent(event);
}

void MonthGridWidget::contextMenuEvent(QContextMenuEvent *event)
{
    const QDate date = dateAt(event->pos());
    if (!date.isValid()) {
        QWidget::contextMenuEvent(event);
        return;
    }

    if (date != m_selectedDate) {
        m_selectedDate = date;
        update();
        emit dateSelected(date);
    }
    emit dateContextMenuRequested(date, event->globalPos());
    event->accept();
}

void MonthGridWidget::moveSelection(int days)
{
    if (!m_selectedDate.isValid() || days == 0)
        return;

    const QVector<int> weekdays = visibleWeekdays();
    QDate candidate = m_selectedDate;
    const int step = days > 0 ? 1 : -1;
    int remaining = qAbs(days);

    // Step one day at a time and skip the columns that are not shown, so the
    // arrow keys walk exactly the cells the user can see.
    while (remaining > 0) {
        candidate = candidate.addDays(step);
        if (weekdays.contains(candidate.dayOfWeek()))
            --remaining;
        if (qAbs(candidate.daysTo(m_selectedDate)) > 400)
            return; // guards against an empty column set
    }

    setSelectedDate(candidate);
}

void MonthGridWidget::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Left:
        moveSelection(-1);
        return;
    case Qt::Key_Right:
        moveSelection(1);
        return;
    case Qt::Key_Up:
        setSelectedDate(m_selectedDate.addDays(-7));
        return;
    case Qt::Key_Down:
        setSelectedDate(m_selectedDate.addDays(7));
        return;
    case Qt::Key_PageUp:
        setSelectedDate(m_selectedDate.addMonths(-1));
        return;
    case Qt::Key_PageDown:
        setSelectedDate(m_selectedDate.addMonths(1));
        return;
    case Qt::Key_Home:
        setSelectedDate(QDate(m_year, m_month, 1));
        return;
    case Qt::Key_End:
        setSelectedDate(QDate(m_year, m_month, 1).addMonths(1).addDays(-1));
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        if (m_selectedDate.isValid())
            emit dateActivated(m_selectedDate);
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

QString MonthGridWidget::tooltipFor(const QDate &date) const
{
    const DaySummary summary = summaryFor(date);

    QStringList lines;
    lines << QLocale().toString(date, QLocale::LongFormat);
    lines << summary.shortDescription();
    if (!summary.meta.note.trimmed().isEmpty())
        lines << summary.meta.note.trimmed();

    if (!summary.lines.isEmpty()) {
        lines << QString();
        const Duration::Format format = m_settings ? m_settings->durationFormat()
                                                   : Duration::Format::HoursAndMinutes;
        for (const DayLine &line : summary.lines) {
            lines << QStringLiteral("%1  %2")
                         .arg(Duration::format(line.minutes, format), line.label);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

bool MonthGridWidget::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        QHelpEvent *helpEvent = static_cast<QHelpEvent *>(event);
        const QDate date = dateAt(helpEvent->pos());
        if (date.isValid())
            QToolTip::showText(helpEvent->globalPos(), tooltipFor(date), this);
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}

QSize MonthGridWidget::sizeHint() const
{
    const int columns = qMax(1, columnCount());
    const int rows = qMax(1, rowCount());
    return QSize(columns * (kMinimumCellWidth + 40),
                 headerHeight() + rows * (kMinimumCellHeight + 30));
}

QSize MonthGridWidget::minimumSizeHint() const
{
    const int columns = qMax(1, columnCount());
    const int rows = qMax(1, rowCount());
    return QSize(columns * kMinimumCellWidth, headerHeight() + rows * kMinimumCellHeight);
}
