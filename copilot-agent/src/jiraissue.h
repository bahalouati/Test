#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

// Turning a Jira issue (REST v2 JSON, fetched with fields=*all&expand=names)
// into a task for Copilot, and the wrap-up comment back. No network here.
namespace JiraIssue {

struct Brief {
    QString key, summary, type, status, priority;
    QString line() const;   // "PROJ-123 · Bug · In Progress · Fix the login timeout"
};
Brief briefOf(const QJsonObject &issue);

// Attachments worth reading: Word/Excel and text-like files, small enough.
struct Attachment { QString name; QUrl url; qint64 size = 0; };
QVector<Attachment> readableAttachments(const QJsonObject &issue, int maxFiles = 6, qint64 maxBytes = 5 * 1024 * 1024);
// Text of a downloaded attachment, or empty if it is not readable.
QString attachmentText(const QString &fileName, const QByteArray &bytes);

// The task text: summary and basics, description, text custom fields by
// their names (acceptance criteria, steps to reproduce...), the latest
// comments, and attachment text, kept within budgetChars overall.
// attachmentTexts: file name -> text.
QString toTask(const QJsonObject &issue, const QHash<QString, QString> &attachmentTexts,
               const QString &extraInstructions, int budgetChars = 8000);

// Draft of the comment posted when the task is done.
QString wrapUpComment(const QString &summary, const QStringList &changedFiles, const QString &branch);

} // namespace JiraIssue
