#include "jiraissue.h"

#include "officefile.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

namespace JiraIssue {

namespace {

QString nameOf(const QJsonValue &v)
{
    const QJsonObject o = v.toObject();
    for (const char *k : {"name", "value", "displayName", "key"}) {
        const QString s = o.value(QLatin1String(k)).toString();
        if (!s.isEmpty())
            return s;
    }
    return v.isString() ? v.toString() : QString();
}

QString joinNames(const QJsonValue &v)
{
    QStringList out;
    for (const QJsonValue &x : v.toArray()) {
        const QString n = nameOf(x);
        if (!n.isEmpty())
            out << n;
    }
    return out.join(QStringLiteral(", "));
}

QString clip(const QString &text, int limit)
{
    const QString t = text.trimmed();
    if (t.size() <= limit)
        return t;
    return t.left(limit) + QStringLiteral("\n[... %1 more characters not shown]").arg(t.size() - limit);
}

// Custom fields that never help understand a task.
bool noiseField(const QString &name)
{
    static const QRegularExpression re(QStringLiteral(
        "^(rank|sprint|development|epic (colou?r|status|name)|flagged|team|parent link|story points?|"
        "start date|target (start|end)|issue color|request participants|organizations|satisfaction.*|"
        "time to .*|\\[chart\\].*)$"), QRegularExpression::CaseInsensitiveOption);
    return re.match(name.trimmed()).hasMatch();
}

} // namespace

QString Brief::line() const
{
    QStringList parts{key};
    for (const QString &p : {type, status})
        if (!p.isEmpty())
            parts << p;
    parts << summary;
    return parts.join(QStringLiteral(" · "));
}

Brief briefOf(const QJsonObject &issue)
{
    const QJsonObject f = issue.value(QStringLiteral("fields")).toObject();
    Brief b;
    b.key = issue.value(QStringLiteral("key")).toString();
    b.summary = f.value(QStringLiteral("summary")).toString();
    b.type = nameOf(f.value(QStringLiteral("issuetype")));
    b.status = nameOf(f.value(QStringLiteral("status")));
    b.priority = nameOf(f.value(QStringLiteral("priority")));
    return b;
}

QVector<Attachment> readableAttachments(const QJsonObject &issue, int maxFiles, qint64 maxBytes)
{
    static const QSet<QString> textExt = {
        QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("csv"), QStringLiteral("json"), QStringLiteral("xml"),
        QStringLiteral("log"), QStringLiteral("ini"), QStringLiteral("yaml"), QStringLiteral("yml"), QStringLiteral("feature"),
        QStringLiteral("sql"), QStringLiteral("cpp"), QStringLiteral("h"), QStringLiteral("hpp"), QStringLiteral("c"),
        QStringLiteral("py"), QStringLiteral("js"), QStringLiteral("ts"), QStringLiteral("java"), QStringLiteral("cs"),
    };
    QVector<Attachment> out;
    const QJsonArray all = issue.value(QStringLiteral("fields")).toObject().value(QStringLiteral("attachment")).toArray();
    for (const QJsonValue &v : all) {
        const QJsonObject a = v.toObject();
        const QString name = a.value(QStringLiteral("filename")).toString();
        const qint64 size = qint64(a.value(QStringLiteral("size")).toDouble());
        const QString ext = QFileInfo(name).suffix().toLower();
        const bool readable = textExt.contains(ext) || Office::kindOf(name) != Office::None;
        if (!readable || size > maxBytes)
            continue;
        out.append({name, QUrl(a.value(QStringLiteral("content")).toString()), size});
        if (out.size() >= maxFiles)
            break;
    }
    return out;
}

QString attachmentText(const QString &fileName, const QByteArray &bytes)
{
    const Office::Kind kind = Office::kindOf(fileName);
    if (kind != Office::None) {
        QString err;
        return Office::extractText(bytes, kind, &err);
    }
    if (bytes.left(8000).contains('\0'))
        return QString();
    return QString::fromUtf8(bytes);
}

QString toTask(const QJsonObject &issue, const QHash<QString, QString> &attachmentTexts,
               const QString &extraInstructions, int budgetChars)
{
    const QJsonObject f = issue.value(QStringLiteral("fields")).toObject();
    const QJsonObject names = issue.value(QStringLiteral("names")).toObject();
    const Brief b = briefOf(issue);

    QStringList head;
    head << QStringLiteral("Jira ticket %1: %2").arg(b.key, b.summary);
    QStringList facts;
    if (!b.type.isEmpty()) facts << QStringLiteral("type %1").arg(b.type);
    if (!b.status.isEmpty()) facts << QStringLiteral("status %1").arg(b.status);
    if (!b.priority.isEmpty()) facts << QStringLiteral("priority %1").arg(b.priority);
    const QString labels = f.value(QStringLiteral("labels")).toVariant().toStringList().join(QStringLiteral(", "));
    if (!labels.isEmpty()) facts << QStringLiteral("labels %1").arg(labels);
    const QString components = joinNames(f.value(QStringLiteral("components")));
    if (!components.isEmpty()) facts << QStringLiteral("components %1").arg(components);
    const QString versions = joinNames(f.value(QStringLiteral("fixVersions")));
    if (!versions.isEmpty()) facts << QStringLiteral("fix versions %1").arg(versions);
    if (!facts.isEmpty())
        head << facts.join(QStringLiteral(", ")) + QLatin1Char('.');

    // Budget: the description first, then custom text fields, then comments,
    // then attachments; each part gets a share and passes on what it leaves.
    int budget = budgetChars;
    QStringList parts;
    parts << head.join(QLatin1Char('\n'));

    const QString description = f.value(QStringLiteral("description")).toString().trimmed();
    if (!description.isEmpty()) {
        const QString d = clip(description, qMin(budget * 2 / 5, 3500));
        parts << QStringLiteral("Description:\n") + d;
        budget -= d.size();
    }

    // Text custom fields by their display names: where acceptance criteria,
    // steps to reproduce and the like live, under ids that differ per instance.
    QStringList custom;
    int customBudget = qMin(budget / 3, 2000);
    for (auto it = f.begin(); it != f.end(); ++it) {
        if (!it.key().startsWith(QLatin1String("customfield_")))
            continue;
        const QString name = names.value(it.key()).toString(it.key());
        if (noiseField(name))
            continue;
        QString value;
        if (it.value().isString())
            value = it.value().toString().trimmed();
        else if (it.value().isObject())
            value = nameOf(it.value());
        else if (it.value().isArray())
            value = joinNames(it.value());
        if (value.isEmpty() || value.startsWith(QLatin1String("com.atlassian.")) || value.startsWith(QLatin1Char('{')))
            continue;
        const QString entry = QStringLiteral("%1: %2").arg(name, clip(value, qMax(200, customBudget)));
        if (entry.size() > customBudget)
            continue;
        custom << entry;
        customBudget -= entry.size();
    }
    if (!custom.isEmpty()) {
        custom.sort(Qt::CaseInsensitive);
        const QString c = QStringLiteral("Other fields:\n") + custom.join(QLatin1Char('\n'));
        parts << c;
        budget -= c.size();
    }

    // The latest comments, newest last, each kept short.
    const QJsonArray comments = f.value(QStringLiteral("comment")).toObject().value(QStringLiteral("comments")).toArray();
    if (!comments.isEmpty()) {
        int commentBudget = qMin(budget / 2, 2500);
        QStringList lines;
        for (int i = comments.size() - 1; i >= 0 && commentBudget > 0; --i) {
            const QJsonObject c = comments.at(i).toObject();
            const QString who = c.value(QStringLiteral("author")).toObject().value(QStringLiteral("displayName")).toString();
            const QString when = c.value(QStringLiteral("created")).toString().left(10);
            const QString line = QStringLiteral("- %1 (%2): %3").arg(who, when,
                                     clip(c.value(QStringLiteral("body")).toString(), 600).replace(QLatin1Char('\n'), QLatin1Char(' ')));
            if (line.size() > commentBudget)
                break;
            lines.prepend(line);
            commentBudget -= line.size();
        }
        if (!lines.isEmpty()) {
            const QString c = QStringLiteral("Comments (latest %1 of %2):\n").arg(lines.size()).arg(comments.size())
                            + lines.join(QLatin1Char('\n'));
            parts << c;
            budget -= c.size();
        }
    }

    // Attachments: the readable ones' text, the rest by name.
    const QJsonArray attachments = f.value(QStringLiteral("attachment")).toArray();
    if (!attachments.isEmpty()) {
        QStringList lines;
        int attBudget = qMax(0, budget);
        const int each = attachmentTexts.isEmpty() ? 0 : attBudget / qMax(1, attachmentTexts.size());
        for (const QJsonValue &v : attachments) {
            const QString name = v.toObject().value(QStringLiteral("filename")).toString();
            const QString text = attachmentTexts.value(name).trimmed();
            if (text.isEmpty() || each < 200) {
                lines << QStringLiteral("- %1 (not included)").arg(name);
                continue;
            }
            lines << QStringLiteral("- %1:\n%2").arg(name, clip(text, each - 80));
        }
        parts << QStringLiteral("Attachments:\n") + lines.join(QLatin1Char('\n'));
    }

    if (!extraInstructions.trimmed().isEmpty())
        parts << QStringLiteral("My notes on this ticket:\n") + extraInstructions.trimmed();
    return parts.join(QStringLiteral("\n\n"));
}

QString wrapUpComment(const QString &summary, const QStringList &changedFiles, const QString &branch)
{
    QStringList out;
    out << (summary.trimmed().isEmpty() ? QStringLiteral("Done.") : summary.trimmed());
    if (!changedFiles.isEmpty()) {
        QStringList files;
        for (const QString &f : changedFiles)
            files << QStringLiteral("* %1").arg(f);
        out << QStringLiteral("Files changed:\n") + files.join(QLatin1Char('\n'));
    }
    if (!branch.isEmpty())
        out << QStringLiteral("Branch: %1").arg(branch);
    return out.join(QStringLiteral("\n\n"));
}

} // namespace JiraIssue
