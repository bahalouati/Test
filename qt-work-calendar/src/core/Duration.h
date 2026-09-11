#ifndef WORKCALENDAR_DURATION_H
#define WORKCALENDAR_DURATION_H

#include <QString>

/*!
 * \file Duration.h
 * \brief Conversions between minutes and the shapes people type or read.
 *
 * Durations are stored as whole minutes everywhere in the application. Minutes
 * are exact: summing a month of quarter hours can never drift the way a column
 * of doubles can, and "7.4h" never has to be rendered as 7.3999999.
 */
namespace Duration {

/*! How a duration is rendered when it is shown to the user. */
enum class Format {
    HoursAndMinutes, //!< "7h 30m"
    Decimal,         //!< "7.50 h"
    Clock            //!< "7:30"
};

/*!
 * \brief Parses the free-form duration people type.
 *
 * Understood, case-insensitively: "7h30", "7h 30m", "7:30", "7,5h", "7.5",
 * "90m", "90 min", "0.25". A bare number is read as hours unless it carries an
 * "m"/"min" suffix. Negative values are rejected.
 *
 * \param text      what the user typed
 * \param[out] ok   set to false when \a text cannot be understood
 * \return the duration in minutes, or 0 when parsing failed
 */
int parseMinutes(const QString &text, bool *ok = nullptr);

/*! Renders \a minutes in the requested \a format. */
QString format(int minutes, Format format = Format::HoursAndMinutes);

/*! Renders \a minutes with a leading sign, for balances ("+2h 15m", "-45m"). */
QString formatSigned(int minutes, Format format = Format::HoursAndMinutes);

/*! Decimal hours, the shape Jira and Excel exports use. */
double toHours(int minutes);

/*! Decimal hours to whole minutes, rounded to nearest. */
int fromHours(double hours);

/*!
 * \brief Rounds \a minutes up to the next multiple of \a incrementMinutes.
 *
 * Rounding up rather than to nearest matches how time is booked in practice: a
 * six-minute interruption is still billed as the smallest bookable unit. An
 * increment of 0 or 1 returns \a minutes unchanged.
 */
int roundUpTo(int minutes, int incrementMinutes);

} // namespace Duration

#endif // WORKCALENDAR_DURATION_H
