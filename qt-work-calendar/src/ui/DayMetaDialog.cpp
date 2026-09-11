#include "ui/DayMetaDialog.h"

#include "ui_DayMetaDialog.h"

#include "core/Duration.h"

#include <QLocale>
#include <QPushButton>

DayMetaDialog::DayMetaDialog(const DayMeta &meta, const Settings &settings, QWidget *parent)
    : QDialog(parent)
    , m_ui(new Ui::DayMetaDialog)
    , m_settings(settings)
    , m_date(meta.date)
{
    m_ui->setupUi(this);

    m_ui->labelDate->setText(QLocale().toString(meta.date, QLocale::LongFormat));

    const QVector<DayType> types = allDayTypes();
    for (DayType type : types)
        m_ui->comboDayType->addItem(dayTypeDisplayName(type), dayTypeToString(type));
    m_ui->comboDayType->setCurrentIndex(
        m_ui->comboDayType->findData(dayTypeToString(meta.type)));

    const bool hasOverride = meta.targetMinutesOverride >= 0;
    m_ui->checkOverrideTarget->setChecked(hasOverride);
    m_ui->lineTargetOverride->setEnabled(hasOverride);
    if (hasOverride)
        m_ui->lineTargetOverride->setText(Duration::format(meta.targetMinutesOverride));

    m_ui->textNote->setPlainText(meta.note);

    connect(m_ui->checkOverrideTarget, &QCheckBox::toggled,
            this, &DayMetaDialog::onOverrideToggled);
    connect(m_ui->lineTargetOverride, &QLineEdit::textChanged,
            this, &DayMetaDialog::onTargetTextChanged);
    connect(m_ui->comboDayType, &QComboBox::currentIndexChanged,
            this, &DayMetaDialog::onDayTypeChanged);
    connect(m_ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QPushButton *resetButton = m_ui->buttonBox->button(QDialogButtonBox::Reset);
    if (resetButton)
        connect(resetButton, &QPushButton::clicked, this, &DayMetaDialog::onResetClicked);

    updateTargetHint();
}

DayMetaDialog::~DayMetaDialog()
{
    delete m_ui;
}

DayMeta DayMetaDialog::dayMeta() const
{
    DayMeta meta;
    meta.date = m_date;
    meta.type = dayTypeFromString(m_ui->comboDayType->currentData().toString());
    meta.note = m_ui->textNote->toPlainText().trimmed();

    if (m_ui->checkOverrideTarget->isChecked()) {
        bool parsed = false;
        const int minutes = Duration::parseMinutes(m_ui->lineTargetOverride->text(), &parsed);
        // An unreadable value is treated as "no override" rather than as zero,
        // which would silently excuse the whole day.
        meta.targetMinutesOverride = parsed ? minutes : -1;
    } else {
        meta.targetMinutesOverride = -1;
    }

    return meta;
}

void DayMetaDialog::onOverrideToggled(bool enabled)
{
    m_ui->lineTargetOverride->setEnabled(enabled);
    updateTargetHint();
}

void DayMetaDialog::onTargetTextChanged(const QString &)
{
    updateTargetHint();
}

void DayMetaDialog::onDayTypeChanged(int)
{
    updateTargetHint();
}

void DayMetaDialog::updateTargetHint()
{
    const DayMeta meta = dayMeta();
    const int target = m_settings.targetMinutesFor(m_date, meta);

    if (target <= 0) {
        m_ui->labelTargetHint->setText(QStringLiteral("no hours expected"));
        return;
    }
    m_ui->labelTargetHint->setText(QStringLiteral("expects %1").arg(Duration::format(target)));
}

void DayMetaDialog::onResetClicked()
{
    m_ui->comboDayType->setCurrentIndex(m_ui->comboDayType->findData(
        dayTypeToString(DayType::Workday)));
    m_ui->checkOverrideTarget->setChecked(false);
    m_ui->lineTargetOverride->clear();
    m_ui->textNote->clear();
    updateTargetHint();
}
