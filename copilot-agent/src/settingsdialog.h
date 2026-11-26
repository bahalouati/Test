#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QString>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QLabel;

struct AppSettings
{
    QString url = QStringLiteral("https://m365.cloud.microsoft/chat");
    // Where Copilot runs:
    //   "edge"      - the user's Edge (or Chrome), driven automatically
    //   "clipboard" - any browser; messages are copied and pasted by hand
    //   "builtin"   - Qt's own WebEngine (too old for the page on Qt 5.12)
    QString mode = QStringLiteral("edge");
    QString browserPath;   // empty: find Edge, then Chrome
    bool autoLoop = true;
    int maxSteps = 25;
    int charLimit = 12000;
    int commandTimeoutSec = 300;
    int replyTimeoutSec = 600;
    int autoSendDelaySec = 5;
    QString selectorsJson;   // overrides for bridge.js, may be empty

    static AppSettings load();
    void save() const;
    QJsonObject selectors() const;
};

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(const AppSettings &s, QWidget *parent = nullptr);
    AppSettings settings() const;

private:
    void validate();

    AppSettings m_s;
    QLineEdit *m_url;
    QComboBox *m_mode;
    QLineEdit *m_browserPath;
    QSpinBox *m_maxSteps;
    QSpinBox *m_charLimit;
    QSpinBox *m_cmdTimeout;
    QSpinBox *m_replyTimeout;
    QSpinBox *m_autoDelay;
    QPlainTextEdit *m_selectors;
    QLabel *m_selectorError;
};
