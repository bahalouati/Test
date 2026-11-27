#pragma once

#include "agentcontroller.h"
#include "git.h"

#include <QHash>
#include <QJsonObject>

#include <functional>
#include "settingsdialog.h"

#include <QMainWindow>

class CopilotBridge;
class QComboBox;
namespace JiraConn { class Client; }
class EdgeSession;
class QSplitter;
class QCheckBox;
class QDragEnterEvent;
class QDropEvent;
class QListWidget;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTextBrowser;
class QToolBar;
class QWebEngineView;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Adds files (absolute paths) to the picked-files list.
    void addFiles(const QStringList &paths);

private:
    void buildUi();
    void wire();
    void applySettings();
    void chooseFolder();
    void setFolder(const QString &path);
    void appendLog(AgentController::LogKind kind, const QString &text);
    void updateButtons();
    void syncFocusFiles();

    // Modes: a free task (as always), a Jira ticket, or a review.
    enum Mode { TaskMode = 0, JiraMode = 1, ReviewMode = 2 };
    Mode mode() const;
    void applyMode();
    void startClicked();
    void startPlainTask();
    void loadJira(const QString &key, std::function<void()> then);
    void startJiraTask();
    bool createJiraBranch();   // false: the user chose Stop
    void jiraWrapUp(const QString &summary);
    void startReview();
    Git::Target reviewTarget() const;
    void openJiraSettings();
    bool eventFilter(QObject *watched, QEvent *event) override;

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    AgentController::Decision approve(AgentAction &a, QString *note);
    void deliverPrompt(const QString &text);
    void deliverByClipboard(const QString &text);
    void useTransportForMode();
    void restoreGeometryForMode();
    void saveGeometryForMode();
    bool isMode(const char *m) const { return m_settings.mode == QLatin1String(m); }
    void pasteReply();

    AppSettings m_settings;
    AgentController *m_agent = nullptr;
    CopilotBridge *m_bridge = nullptr;
    EdgeSession *m_edge = nullptr;
    QString m_queuedPrompt;        // waiting for Edge to connect
    bool m_edgeFellBack = false;   // Edge failed; clipboard used instead
    QString m_geometryMode;        // mode whose geometry is applied
    QSplitter *m_split = nullptr;
    QAction *m_openEdge = nullptr;
    QAction *m_describe = nullptr;

    QWebEngineView *m_view = nullptr;
    QLineEdit *m_address = nullptr;
    QLineEdit *m_folder = nullptr;
    QPlainTextEdit *m_task = nullptr;
    QPushButton *m_start = nullptr;
    QPushButton *m_followUp = nullptr;
    QPushButton *m_stop = nullptr;
    QPushButton *m_sendResults = nullptr;
    QPushButton *m_readReply = nullptr;
    QPushButton *m_copyPrompt = nullptr;
    QPushButton *m_pasteReply = nullptr;
    QPushButton *m_undo = nullptr;
    QToolBar *m_browserBar = nullptr;
    QCheckBox *m_autoLoop = nullptr;
    QSpinBox *m_maxSteps = nullptr;
    QTextBrowser *m_log = nullptr;
    QListWidget *m_files = nullptr;
    QCheckBox *m_onlyFiles = nullptr;
    QPushButton *m_removeFiles = nullptr;
    QComboBox *m_mode = nullptr;
    QCheckBox *m_planFirst = nullptr;
    QWidget *m_jiraRow = nullptr;
    QLineEdit *m_jiraKey = nullptr;
    QLabel *m_jiraInfo = nullptr;
    QWidget *m_reviewRow = nullptr;
    QComboBox *m_reviewTarget = nullptr;
    QLineEdit *m_reviewBase = nullptr;
    JiraConn::Client *m_jira = nullptr;
    QJsonObject m_jiraIssue;               // the loaded ticket
    QHash<QString, QString> m_jiraAttachments;
    QString m_activeJiraKey;               // the ticket the running task is for
    QString m_jiraBranch;
    QString m_reviewDescription;
    QLabel *m_status = nullptr;
};
