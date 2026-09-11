#include "Duration.h"

#include <QRegularExpression>
#include <QStringList>
#include <cmath>

namespace {

/*!
 * \brief Matches everything parseMinutes() accepts.
 *
 * group 1 - the hour part, or the whole value when no unit follows
 * group 2 - the separator that proves group 1 was hours ("h", ":" or "h ")
 * group 3 - the minute part
 * group 4 - a minute-only unit ("m", "min", "minutes")
 */
const char *kDurationPattern =
    "^\\s*(\\d+(?:[.,]\\d+)?)\\s*(?:(h|hr|hrs|hour|hours|:)\\s*(\\d+)?\\s*(?:m|min|mins|minute|minutes)?"
    "|(m|min|mins|minute|minutes))?\\s*$";

} // namespace

int Duration::parseMinutes(const QString &text, bool *ok)
{
    if (ok)
        *ok = false;

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return 0;

    static const QRegularExpression pattern(QString::fromLatin1(kDurationPattern),
                                            QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(trimmed);
    if (!match.hasMatch())
        return 0;

    QString firstNumber = match.captured(1);
    firstNumber.replace(QLatin1Char(','), QLatin1Char('.'));

    const QString hourSeparator = match.captured(2);
    const QString minutePart = match.captured(3);
    const QString minuteUnit = match.captured(4);

    bool numberOk = false;
    const double firstValue = firstNumber.toDouble(&numberOk);
    if (!numberOk)
        return 0;

    int minutes = 0;
    if (!minuteUnit.isEmpty()) {
        // "90m" - the single number is already minutes.
        minutes = static_cast<int>(std::llround(firstValue));
    } else if (!hourSeparator.isEmpty()) {
        // "7h", "7h30", "7:30" - hours, optionally followed by minutes.
        minutes = static_cast<int>(std::llround(firstValue * 60.0));
        if (!minutePart.isEmpty())
            minutes += minutePart.toInt();
    } else {
        // A bare "7" or "7.5" means hours, which is what people type most.
        minutes = static_cast<int>(std::llround(firstValue * 60.0));
    }

    if (minutes < 0)
        return 0;

    if (ok)
        *ok = true;
    return minutes;
}

QString Duration::format(int minutes, Format format)
{
    const int absolute = qAbs(minutes);
    const int hours = absolute / 60;
    const int remainder = absolute % 60;

    switch (format) {
    case Format::Decimal:
        return QStringLiteral("%1 h").arg(toHours(absolute), 0, 'f', 2);
    case Format::Clock:
        return QStringLiteral("%1:%2").arg(hours).arg(remainder, 2, 10, QLatin1Char('0'));
    case Format::HoursAndMinutes:
        break;
    }

    if (hours == 0)
        return QStringLiteral("%1m").arg(remainder);
    if (remainder == 0)
        return QStringLiteral("%1h").arg(hours);
    return QStringLiteral("%1h %2m").arg(hours).arg(remainder);
}

QString Duration::formatSigned(int minutes, Format format)
{
    if (minutes == 0)
        return QStringLiteral("0");
    const QString sign = minutes < 0 ? QStringLiteral("-") : QStringLiteral("+");
    return sign + Duration::format(qAbs(minutes), format);
}

double Duration::toHours(int minutes)
{
    return static_cast<double>(minutes) / 60.0;
}

int Duration::fromHours(double hours)
{
    return static_cast<int>(std::llround(hours * 60.0));
}

int Duration::roundUpTo(int minutes, int incrementMinutes)
{
    if (incrementMinutes <= 1 || minutes <= 0)
        return minutes;
    const int remainder = minutes % incrementMinutes;
    if (remainder == 0)
        return minutes;
    return minutes + (incrementMinutes - remainder);
}
