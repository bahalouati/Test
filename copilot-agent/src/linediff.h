#pragma once

#include <QString>

namespace LineDiff {

// A unified diff (---/+++/@@ hunks) of two texts, for the approval dialog.
// Large inputs fall back to a cheaper comparison that trims the common head
// and tail, so a 10 000-line file cannot freeze the window.
QString unified(const QString &before, const QString &after,
                const QString &path, int context = 3);

struct Stats { int added = 0; int removed = 0; };
Stats stats(const QString &diff);

} // namespace LineDiff
