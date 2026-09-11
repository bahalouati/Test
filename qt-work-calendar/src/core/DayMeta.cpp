#include "core/DayMeta.h"

bool DayMeta::isEmpty() const
{
    return type == DayType::Workday
           && targetMinutesOverride < 0
           && note.trimmed().isEmpty();
}
