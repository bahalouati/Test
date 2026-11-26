#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QString>

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QLabel;

struct AppSettings
{
    QString url = QStringLiteral("https://m365.cloud.microsoft/chat");
    // Copilot in your own browser, messages passed through the clipboard.
    // The default before Qt 5.15, whose Chromium is too old for the page.
    bool externalBrowser = QT_VERSION < QT_VERSION_CHECK(5, 15, 0);
    bool autoLoop = true;
    int maxSteps = 25;
    int charLimit = 12000;
    int commandTimeoutSec = 300;
    int replyTimeoutSec = 600;
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
    QCheckBox *m_external;
    QSpinBox *m_maxSteps;
    QSpinBox *m_charLimit;
    QSpinBox *m_cmdTimeout;
    QSpinBox *m_replyTimeout;
    QPlainTextEdit *m_selectors;
    QLabel *m_selectorError;
};
