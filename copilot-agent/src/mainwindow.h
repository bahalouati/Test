#pragma once

#include "agentcontroller.h"
#include "settingsdialog.h"

#include <QMainWindow>

class CopilotBridge;
class EdgeSession;
class QSplitter;
class QCheckBox;
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

private:
    void buildUi();
    void wire();
    void applySettings();
    void chooseFolder();
    void setFolder(const QString &path);
    void appendLog(AgentController::LogKind kind, const QString &text);
    void updateButtons();
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
    QLabel *m_status = nullptr;
};
