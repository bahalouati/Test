#include "linediff.h"

#include <QStringList>
#include <QVector>

#include <algorithm>
#include <vector>

namespace LineDiff {

namespace {

enum Op { Keep, Del, Add };
struct Edit { Op op; QString line; };

QStringList splitLines(const QString &text)
{
    if (text.isEmpty())
        return {};
    QString t = text;
    t.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    QStringList lines = t.split(QLatin1Char('\n'));
    if (t.endsWith(QLatin1Char('\n')))
        lines.removeLast();
    return lines;
}

// Classic LCS on the part between the common prefix and suffix.
QVector<Edit> lcsEdits(const QStringList &a, const QStringList &b)
{
    const int n = int(a.size()), m = int(b.size());
    std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
    for (int i = n - 1; i >= 0; --i)
        for (int j = m - 1; j >= 0; --j)
            dp[i][j] = a[i] == b[j] ? dp[i + 1][j + 1] + 1 : std::max(dp[i + 1][j], dp[i][j + 1]);

    QVector<Edit> out;
    int i = 0, j = 0;
    while (i < n && j < m) {
        if (a[i] == b[j]) { out.append({Keep, a[i]}); ++i; ++j; }
        else if (dp[i + 1][j] >= dp[i][j + 1]) out.append({Del, a[i++]});
        else out.append({Add, b[j++]});
    }
    while (i < n) out.append({Del, a[i++]});
    while (j < m) out.append({Add, b[j++]});
    return out;
}

QVector<Edit> edits(const QStringList &a, const QStringList &b)
{
    int pre = 0;
    while (pre < a.size() && pre < b.size() && a[pre] == b[pre])
        ++pre;
    int suf = 0;
    while (suf < a.size() - pre && suf < b.size() - pre
           && a[a.size() - 1 - suf] == b[b.size() - 1 - suf])
        ++suf;

    QVector<Edit> out;
    for (int i = 0; i < pre; ++i)
        out.append({Keep, a[i]});

    const QStringList midA = a.mid(pre, a.size() - pre - suf);
    const QStringList midB = b.mid(pre, b.size() - pre - suf);
    if (qint64(midA.size()) * midB.size() <= 4'000'000) {
        out += lcsEdits(midA, midB);
    } else {
        for (const QString &l : midA) out.append({Del, l});
        for (const QString &l : midB) out.append({Add, l});
    }
    for (int i = int(a.size()) - suf; i < a.size(); ++i)
        out.append({Keep, a[i]});
    return out;
}

} // namespace

QString unified(const QString &before, const QString &after, const QString &path, int context)
{
    const QStringList a = splitLines(before), b = splitLines(after);
    const QVector<Edit> e = edits(a, b);

    QString out = QStringLiteral("--- %1\n+++ %2\n")
                      .arg(before.isEmpty() ? QStringLiteral("/dev/null") : QStringLiteral("a/") + path,
                           QStringLiteral("b/") + path);
    bool any = false;
    int idx = 0;
    const int n = int(e.size());
    while (idx < n) {
        while (idx < n && e[idx].op == Keep)
            ++idx;
        if (idx >= n)
            break;
        any = true;
        // Grow the hunk while the next change is within 2*context lines.
        int start = qMax(0, idx - context);
        int end = idx;
        while (true) {
            while (end < n && e[end].op != Keep)
                ++end;
            int k = end;
            while (k < n && e[k].op == Keep && k - end < 2 * context)
                ++k;
            if (k < n && e[k].op != Keep) { end = k; continue; }
            end = qMin(n, end + context);
            break;
        }
        // Line numbers at the hunk start.
        int la = 1, lb = 1;
        for (int k = 0; k < start; ++k) {
            if (e[k].op != Add) ++la;
            if (e[k].op != Del) ++lb;
        }
        int ca = 0, cb = 0;
        QString body;
        for (int k = start; k < end; ++k) {
            const QChar sign = e[k].op == Keep ? QLatin1Char(' ') : e[k].op == Del ? QLatin1Char('-') : QLatin1Char('+');
            if (e[k].op != Add) ++ca;
            if (e[k].op != Del) ++cb;
            body += sign + e[k].line + QLatin1Char('\n');
        }
        out += QStringLiteral("@@ -%1,%2 +%3,%4 @@\n").arg(ca ? la : la - 1).arg(ca).arg(cb ? lb : lb - 1).arg(cb);
        out += body;
        idx = end;
    }
    if (!any)
        out += QStringLiteral("(no changes)\n");
    return out;
}

Stats stats(const QString &diff)
{
    Stats s;
    const QStringList lines = diff.split(QLatin1Char('\n'));
    for (const QString &l : lines) {
        if (l.startsWith(QLatin1String("+++")) || l.startsWith(QLatin1String("---")))
            continue;
        if (l.startsWith(QLatin1Char('+'))) ++s.added;
        else if (l.startsWith(QLatin1Char('-'))) ++s.removed;
    }
    return s;
}

} // namespace LineDiff
