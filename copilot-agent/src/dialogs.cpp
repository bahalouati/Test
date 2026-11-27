#include "dialogs.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <algorithm>

// ------------------------------------------------------------ Jira settings

JiraSettingsDialog::JiraSettingsDialog(const JiraConn::Credentials &c, QWidget *parent)
    : QDialog(parent), m_client(new JiraConn::Client(this))
{
    setWindowTitle(tr("Jira settings"));
    resize(560, 0);
    auto *lay = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_url = new QLineEdit(c.baseUrl, this);
    m_url->setPlaceholderText(QStringLiteral("https://jira.example.com"));
    form->addRow(tr("Server URL:"), m_url);
    m_mode = new QComboBox(this);
    m_mode->addItem(tr("Jira Server / Data Center: personal access token"), int(JiraConn::AuthMode::Bearer));
    m_mode->addItem(tr("Jira Cloud: e-mail + API token"), int(JiraConn::AuthMode::Basic));
    m_mode->setCurrentIndex(c.mode == JiraConn::AuthMode::Bearer ? 0 : 1);
    form->addRow(tr("Sign in with:"), m_mode);
    m_user = new QLineEdit(c.username, this);
    form->addRow(tr("E-mail:"), m_user);
    m_token = new QLineEdit(c.token, this);
    m_token->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Token:"), m_token);
    m_remember = new QCheckBox(tr("Remember the token on this computer (stored in plain text in the settings)"), this);
    m_remember->setChecked(c.rememberToken);
    form->addRow(QString(), m_remember);
    auto *caRow = new QHBoxLayout;
    m_ca = new QLineEdit(c.caCertificatePath, this);
    m_ca->setPlaceholderText(tr("Optional: your company's CA certificate (.pem)"));
    auto *browse = new QPushButton(tr("Choose..."), this);
    caRow->addWidget(m_ca, 1);
    caRow->addWidget(browse);
    form->addRow(tr("CA certificate:"), caRow);
    m_insecure = new QCheckBox(tr("Accept invalid certificates (only for a test server)"), this);
    m_insecure->setChecked(c.allowInvalidCertificates);
    form->addRow(QString(), m_insecure);
    lay->addLayout(form);

    auto *hint = new QLabel(tr("The same connection as JiraDesk: if JiraDesk is set up on this computer, its settings "
                               "were filled in. JIRA_BASE_URL, JIRA_USER and JIRA_API_TOKEN override them."), this);
    hint->setWordWrap(true);
    lay->addWidget(hint);
    m_result = new QLabel(this);
    m_result->setWordWrap(true);
    lay->addWidget(m_result);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto *testBtn = buttons->addButton(tr("Test connection"), QDialogButtonBox::ActionRole);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(testBtn, &QPushButton::clicked, this, &JiraSettingsDialog::test);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("CA certificate"), QString(),
                                                       tr("Certificates (*.pem *.crt *.cer);;All files (*)"));
        if (!f.isEmpty())
            m_ca->setText(f);
    });
    connect(m_mode, qOverload<int>(&QComboBox::currentIndexChanged), this, &JiraSettingsDialog::updateFields);
    updateFields();
}

void JiraSettingsDialog::updateFields()
{
    const bool cloud = m_mode->currentData().toInt() == int(JiraConn::AuthMode::Basic);
    m_user->setEnabled(cloud);
    m_user->setPlaceholderText(cloud ? tr("you@company.com") : tr("not needed for a personal access token"));
}

JiraConn::Credentials JiraSettingsDialog::credentials() const
{
    JiraConn::Credentials c;
    c.baseUrl = JiraConn::normalizeBaseUrl(m_url->text());
    c.mode = JiraConn::AuthMode(m_mode->currentData().toInt());
    c.username = m_user->text().trimmed();
    c.token = m_token->text().trimmed();
    c.rememberToken = m_remember->isChecked();
    c.caCertificatePath = m_ca->text().trimmed();
    c.allowInvalidCertificates = m_insecure->isChecked();
    return c;
}

void JiraSettingsDialog::test()
{
    m_client->setCredentials(credentials());
    m_result->setText(tr("Connecting..."));
    m_client->fetchMyself([this](const QJsonValue &body, const QString &error) {
        if (!error.isEmpty()) {
            m_result->setText(tr("Failed: %1").arg(error));
            return;
        }
        m_result->setText(tr("Connected as %1.").arg(body.toObject().value(QStringLiteral("displayName")).toString()));
    });
}

// ---------------------------------------------------------------------- plan

PlanDialog::PlanDialog(const QString &plan, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Copilot's plan"));
    resize(760, 560);
    auto *lay = new QVBoxLayout(this);
    auto *title = new QLabel(tr("Copilot proposes this plan. Nothing has been changed yet."), this);
    QFont f = title->font();
    f.setBold(true);
    title->setFont(f);
    lay->addWidget(title);
    auto *view = new QTextBrowser(this);
    view->setPlainText(plan);
    lay->addWidget(view, 1);
    m_note = new QPlainTextEdit(this);
    m_note->setPlaceholderText(tr("Optional: what to change in the plan, or a note to go with your approval"));
    m_note->setMaximumHeight(80);
    lay->addWidget(m_note);
    auto *buttons = new QDialogButtonBox(this);
    auto *approve = buttons->addButton(tr("Approve plan"), QDialogButtonBox::AcceptRole);
    auto *revise = buttons->addButton(tr("Ask for changes"), QDialogButtonBox::ActionRole);
    auto *stop = buttons->addButton(tr("Stop agent"), QDialogButtonBox::DestructiveRole);
    approve->setDefault(true);
    lay->addWidget(buttons);
    connect(approve, &QPushButton::clicked, this, [this] { m_decision = AgentController::PlanDecision::Approve; accept(); });
    connect(revise, &QPushButton::clicked, this, [this] {
        if (m_note->toPlainText().trimmed().isEmpty()) {
            m_note->setFocus();
            m_note->setPlaceholderText(tr("Write what should change in the plan first"));
            return;
        }
        m_decision = AgentController::PlanDecision::Revise;
        accept();
    });
    connect(stop, &QPushButton::clicked, this, [this] { m_decision = AgentController::PlanDecision::Stop; reject(); });
}

QString PlanDialog::note() const
{
    return m_note->toPlainText();
}

// ------------------------------------------------------------------ findings

static int severityRank(const QString &s)
{
    const QString l = s.toLower();
    return l.startsWith(QLatin1String("high")) || l == QLatin1String("critical") ? 0
         : l.startsWith(QLatin1String("med")) ? 1 : 2;
}

QString FindingsDialog::asText(const QJsonArray &findings, const QString &target)
{
    QStringList out;
    out << QStringLiteral("Review of %1: %2 findings").arg(target).arg(findings.size());
    for (const QJsonValue &v : findings) {
        const QJsonObject o = v.toObject();
        const int line = o.value(QStringLiteral("line")).toInt();
        out << QStringLiteral("\n[%1] %2%3: %4\n%5")
                   .arg(o.value(QStringLiteral("severity")).toString(QStringLiteral("?")),
                        o.value(QStringLiteral("file")).toString(),
                        line > 0 ? QStringLiteral(":%1").arg(line) : QString(),
                        o.value(QStringLiteral("title")).toString(),
                        o.value(QStringLiteral("detail")).toString());
    }
    return out.join(QLatin1Char('\n'));
}

FindingsDialog::FindingsDialog(const QJsonArray &findings, const QString &target, QWidget *parent)
    : QDialog(parent), m_findings(findings)
{
    setWindowTitle(tr("Review findings"));
    resize(860, 600);
    auto *lay = new QVBoxLayout(this);
    auto *title = new QLabel(findings.isEmpty() ? tr("No problems found in %1.").arg(target)
                                                : (findings.size() == 1 ? tr("1 finding in %1").arg(target)
                                                                        : tr("%1 findings in %2").arg(findings.size()).arg(target)), this);
    QFont f = title->font();
    f.setBold(true);
    title->setFont(f);
    lay->addWidget(title);

    m_table = new QTableWidget(int(findings.size()), 3, this);
    m_table->setHorizontalHeaderLabels({tr("Severity"), tr("Where"), tr("Finding")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    // Most severe first, otherwise in Copilot's order.
    QVector<int> order;
    for (int i = 0; i < findings.size(); ++i)
        order << i;
    std::stable_sort(order.begin(), order.end(), [&findings](int a, int b) {
        return severityRank(findings.at(a).toObject().value(QStringLiteral("severity")).toString())
             < severityRank(findings.at(b).toObject().value(QStringLiteral("severity")).toString());
    });
    for (int row = 0; row < order.size(); ++row) {
        const int i = order.at(row);
        const QJsonObject o = findings.at(i).toObject();
        const QString sev = o.value(QStringLiteral("severity")).toString(QStringLiteral("?"));
        auto *sevItem = new QTableWidgetItem(sev);
        sevItem->setData(Qt::UserRole, i);
        sevItem->setData(Qt::UserRole + 1, severityRank(sev));
        const QColor color = severityRank(sev) == 0 ? QColor(0xcf, 0x22, 0x2e)
                           : severityRank(sev) == 1 ? QColor(0x9a, 0x67, 0x00) : QColor(0x57, 0x60, 0x6a);
        sevItem->setForeground(color);
        const int line = o.value(QStringLiteral("line")).toInt();
        m_table->setItem(row, 0, sevItem);
        m_table->setItem(row, 1, new QTableWidgetItem(o.value(QStringLiteral("file")).toString()
                                                      + (line > 0 ? QStringLiteral(":%1").arg(line) : QString())));
        m_table->setItem(row, 2, new QTableWidgetItem(o.value(QStringLiteral("title")).toString()));
    }
    m_table->resizeColumnsToContents();
    lay->addWidget(m_table, 2);

    m_detail = new QTextBrowser(this);
    m_detail->setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    lay->addWidget(m_detail, 1);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        const auto items = m_table->selectedItems();
        if (items.isEmpty())
            return;
        const int i = m_table->item(items.first()->row(), 0)->data(Qt::UserRole).toInt();
        const QJsonObject o = m_findings.at(i).toObject();
        m_detail->setPlainText(o.value(QStringLiteral("title")).toString() + QStringLiteral("\n\n")
                               + o.value(QStringLiteral("detail")).toString());
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *copy = buttons->addButton(tr("Copy as text"), QDialogButtonBox::ActionRole);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(copy, &QPushButton::clicked, this, [this, target] {
        QApplication::clipboard()->setText(asText(m_findings, target));
    });
    if (m_table->rowCount() > 0)
        m_table->selectRow(0);
}
