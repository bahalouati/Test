#include "guidedform.h"
#include "qssloader.h"
#include "ui_guidedform.h"      // generated from guidedform.ui

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

namespace {

void repolish(QWidget *w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

bool matches(const QString &s, const char *re)
{
    return QRegularExpression(QString::fromUtf8(re)).match(s).hasMatch();
}

int digitCount(const QString &s)
{
    int n = 0;
    for (const QChar c : s) n += c.isDigit();
    return n;
}

} // namespace

// =====================================================================================
//  FIELD DEFINITIONS - this is the only place that knows what the form asks for.
// =====================================================================================
void GuidedForm::buildSteps()
{
    auto add = [this](Step st, const QVector<FieldSpec> &specs) {
        for (const FieldSpec &s : specs) {
            auto *f = new GuidedField(s);
            st.fields.push_back(f);
            m_byKey[s.key] = f;
            connect(f, &GuidedField::activated, this, &GuidedForm::onActivated);
            connect(f, &GuidedField::edited, this, &GuidedForm::onEdited);
            connect(f, &GuidedField::submitted, this, &GuidedForm::onSubmitted);
        }
        m_steps.push_back(st);
    };

    // ---------------- step 1: about you
    add({"About you", "Name and contact", "Tell us about you",
         "We use this to personalise your workspace. It takes under a minute.", {}}, {
        {"first", "First name", "Ada", "The name people call you. We use it to greet you inside the app.", "Ada", false, false, {
            {"At least 2 letters", "Use at least 2 letters", [](const QString &s) { return s.size() >= 2; }},
            {"Letters, spaces, - and ' only", "Names can only contain letters, spaces, hyphens and apostrophes",
             [](const QString &s) { return matches(s, "^[\\p{L}][\\p{L} '\\-]*$"); }}}},
        {"last", "Last name", "Lovelace", "Your family name, as it appears on your invoices.", "Lovelace", false, false, {
            {"At least 2 letters", "Use at least 2 letters", [](const QString &s) { return s.size() >= 2; }},
            {"Letters, spaces, - and ' only", "Names can only contain letters, spaces, hyphens and apostrophes",
             [](const QString &s) { return matches(s, "^[\\p{L}][\\p{L} '\\-]*$"); }}}},
        {"email", "Work email", "ada@company.com", "We send your sign-in link and receipts here. Use an address you check daily.",
         "ada@analytical.dev", false, false, {
            {"Has an @ sign with a name before it", "Add an @ sign, like name@company.com",
             [](const QString &s) { return s.indexOf('@') > 0; }},
            {"Has a domain like company.com", "Finish the domain, like name@company.com",
             [](const QString &s) { return matches(s, "^[^@\\s]+@[^@\\s]+\\.[A-Za-z]{2,}$"); }}}},
        {"phone", "Phone", "+216 20 123 456", "Only used for account recovery. Skip it if you prefer.", "+216 20 123 456", true, false, {
            {"Digits, spaces, + and - only", "Only digits, spaces, + and - are allowed",
             [](const QString &s) { return matches(s, "^\\+?[0-9 \\-]+$"); }},
            {"8 to 15 digits", "Phone numbers have 8 to 15 digits",
             [](const QString &s) { return digitCount(s) >= 8 && digitCount(s) <= 15; }}}},
    });

    // ---------------- step 2: your work
    add({"Your work", "Role and company", "Where do you work?",
         "This helps us suggest the right templates and defaults.", {}}, {
        {"role", "Job title", "Product designer", "What you do day to day. A short title is fine.", "Mathematician", false, false, {
            {"At least 2 characters", "Use at least 2 characters", [](const QString &s) { return s.size() >= 2; }}}},
        {"company", "Company", "Analytical Engines Ltd", "The name of your company or team.", "Analytical Engines Ltd", false, false, {
            {"At least 2 characters", "Use at least 2 characters", [](const QString &s) { return s.size() >= 2; }}}},
        {"website", "Website", "analytical.dev", "Your company site. We use it to fetch your logo.", "analytical.dev", true, false, {
            {"No spaces", "Websites cannot contain spaces", [](const QString &s) { return !s.contains(' '); }},
            {"Looks like a domain (example.com)", "Try something like example.com",
             [](const QString &s) { return matches(s, "^(https?://)?([A-Za-z0-9-]+\\.)+[A-Za-z]{2,}(/\\S*)?$"); }}}},
        {"city", "City", "Tunis", "Used to set your time zone and currency.", "Tunis", true, false, {
            {"At least 2 letters", "Use at least 2 letters", [](const QString &s) { return s.size() >= 2; }}}},
    });

    // ---------------- step 3: account
    add({"Secure it", "Username and password", "Secure your account",
         "Last step. Pick a username and a strong password.", {}}, {
        {"username", "Username", "ada_l", "This is how your teammates will mention you. You can change it later.", "ada_l", false, false, {
            {"3 to 20 characters", "Use 3 to 20 characters", [](const QString &s) { return s.size() >= 3 && s.size() <= 20; }},
            {"Starts with a letter", "Start with a letter", [](const QString &s) { return matches(s, "^[A-Za-z]"); }},
            {"Letters, numbers and _ only", "Only letters, numbers and underscores",
             [](const QString &s) { return matches(s, "^[A-Za-z0-9_]+$"); }}}},
        {"password", "Password", "Choose a strong password", "Mix different kinds of characters. Longer is better than clever.",
         "Analytical#1843", false, true, {
            {"At least 8 characters", "Use at least 8 characters", [](const QString &s) { return s.size() >= 8; }},
            {"An uppercase letter", "Add an uppercase letter", [](const QString &s) { return matches(s, "[A-Z]"); }},
            {"A number", "Add a number", [](const QString &s) { return matches(s, "[0-9]"); }},
            {"A symbol like ! ? # %", "Add a symbol such as ! ? # %", [](const QString &s) { return matches(s, "[^A-Za-z0-9]"); }}}},
        {"confirm", "Confirm password", "Type it once more", "Re-enter the password so we know there is no typo.", "Same as above", false, true, {
            {"Matches your password", "The two passwords do not match yet", [this](const QString &s) {
                 const GuidedField *p = m_byKey.value("password");
                 return p && s == p->text();
             }}}},
    });
}

// =====================================================================================
//  UI  (the layout itself is in guidedform.ui)
// =====================================================================================
GuidedForm::GuidedForm(QWidget *parent) : QWidget(parent), ui(new Ui::GuidedForm)
{
    ui->setupUi(this);                  // creates every static widget and layout declared in guidedform.ui
    setStyleSheet(loadQss("guided"));   // the look lives in themes/guided.qss (selects widgets by their "role")
    buildSteps();                       // the data: which fields exist, their help text and rules
    buildUi();                          // glue: put the fields into the .ui pages and connect signals
    goToStep(0);
}

GuidedForm::~GuidedForm() { delete ui; }

void GuidedForm::buildUi()
{
    // ---- 1. give the behaviour code short names for the widgets it needs (all declared in the .ui)
    m_ring = ui->ring;
    m_ringSub = ui->ringSub;
    m_stepList = ui->stepList;
    m_stack = ui->stack;
    m_pages = ui->formPages;
    m_eyebrow = ui->stepEyebrow;
    m_heading = ui->stepHeading;
    m_blurb = ui->stepBlurb;
    m_back = ui->backButton;
    m_next = ui->nextButton;
    m_sumTitle = ui->successTitle;
    m_coachTitle = ui->coachTitle;
    m_coachText = ui->coachText;
    m_exampleTag = ui->exampleTag;
    m_example = ui->example;
    m_reqHead = ui->reqHead;
    m_preview = ui->preview;
    QLabel *vals[4] = {ui->sumVal1, ui->sumVal2, ui->sumVal3, ui->sumVal4};
    QLabel *rules[4] = {ui->rule1, ui->rule2, ui->rule3, ui->rule4};
    for (int i = 0; i < 4; ++i) {
        m_sumValues[i] = vals[i];
        m_rules[i] = rules[i];
    }

    // ---- 2. the fields are DATA, not layout, so they are created in buildSteps() and inserted here into the
    //         empty page layouts of the .ui (stepLayout1..3). Each page ends with a spacer that is already in
    //         the .ui, so fields go in front of it. One page per step: add a page in Designer AND a step in
    //         buildSteps() to grow the form.
    QVBoxLayout *pageLayouts[] = {ui->stepLayout1, ui->stepLayout2, ui->stepLayout3};
    const int pageCount = int(sizeof pageLayouts / sizeof *pageLayouts);
    if (m_steps.size() != pageCount || m_pages->count() != pageCount)
        qWarning("guidedform.ui has %d step pages but buildSteps() defines %d steps",
                 int(m_pages->count()), int(m_steps.size()));
    for (int s = 0; s < qMin<int>(pageCount, m_steps.size()); ++s)
        for (int k = 0; k < m_steps[s].fields.size(); ++k)
            pageLayouts[s]->insertWidget(k, m_steps[s].fields[k]);

    // ---- 3. connections that the .ui cannot express
    connect(m_stepList, &StepList::stepClicked, this, [this](int i) {
        // you may jump back freely, or forward only if everything before is complete
        for (int k = 0; k < i; ++k)
            if (!stepComplete(k)) return;
        m_stack->setCurrentIndex(0);
        goToStep(i);
    });
    connect(m_back, &QPushButton::clicked, this, &GuidedForm::back);
    connect(m_next, &QPushButton::clicked, this, &GuidedForm::next);
    connect(ui->againButton, &QPushButton::clicked, this, &GuidedForm::reset);
}

// =====================================================================================
//  BEHAVIOUR
// =====================================================================================
bool GuidedForm::stepComplete(int i) const
{
    for (const GuidedField *f : m_steps[i].fields)
        if (!f->isValid())
            return false;
    return true;
}

int GuidedForm::requiredTotal() const
{
    int n = 0;
    for (const Step &s : m_steps)
        for (const GuidedField *f : s.fields)
            n += !f->spec().optional;
    return n;
}

int GuidedForm::requiredDone() const
{
    int n = 0;
    for (const Step &s : m_steps)
        for (const GuidedField *f : s.fields)
            n += (!f->spec().optional && f->isDone());
    return n;
}

void GuidedForm::updateAll()
{
    const int total = requiredTotal(), done = requiredDone();
    m_ring->setPercent(total ? done * 100 / total : 0);
    m_ringSub->setText(QString("%1 of %2 required fields").arg(done).arg(total));

    QVector<StepList::Step> ss;
    for (int i = 0; i < m_steps.size(); ++i)
        ss.push_back({m_steps[i].title, m_steps[i].subtitle, stepComplete(i) && (i != m_step || m_finished)});
    m_stepList->setSteps(ss, m_step);

    m_next->setEnabled(stepComplete(m_step));
    for (const Step &s : m_steps)
        for (const GuidedField *f : s.fields)
            m_preview->setValue(f->spec().key, f->text());
}

void GuidedForm::updateCoach(GuidedField *f)
{
    if (!f) {
        m_coachTitle->setText("Let's get started");
        m_coachText->setText("Click any field. I will explain what it needs and check it as you type.");
        m_exampleTag->hide();
        m_example->hide();
        m_reqHead->hide();
        for (QLabel *r : m_rules) r->hide();
        return;
    }
    const FieldSpec &sp = f->spec();
    m_coachTitle->setText(sp.optional ? sp.label + "  (optional)" : sp.label);
    m_coachText->setText(sp.help);
    m_exampleTag->setVisible(!sp.example.isEmpty());
    m_example->setVisible(!sp.example.isEmpty());
    m_example->setText(sp.password ? QString::fromUtf8("••••••••••") : sp.example);
    m_reqHead->setVisible(!sp.rules.isEmpty());

    const QVector<bool> st = f->ruleStates();
    for (int i = 0; i < 4; ++i) {
        QLabel *r = m_rules[i];
        if (i >= sp.rules.size()) { r->hide(); continue; }
        const bool ok = st[i];
        const bool bad = !ok && f->hasError();
        r->setProperty("state", ok ? "ok" : (bad ? "bad" : "todo"));
        r->setText((ok ? QString::fromUtf8("✓") : (bad ? QString::fromUtf8("✕") : QString::fromUtf8("○")))
                   + "   " + sp.rules[i].text);
        repolish(r);
        r->show();
    }
}

void GuidedForm::onActivated(GuidedField *f)
{
    m_active = f;
    // fade every other field of the visible step, un-fade the active one
    for (GuidedField *g : m_steps[m_step].fields)
        g->setDimmed(g != f);
    updateCoach(f);
}

void GuidedForm::onEdited(GuidedField *f)
{
    if (f->spec().key == "password")
        if (GuidedField *c = m_byKey.value("confirm"))
            c->refresh();                      // "matches your password" depends on this field
    updateAll();
    if (f == m_active)
        updateCoach(f);
}

void GuidedForm::onSubmitted(GuidedField *f)
{
    const auto &fields = m_steps[m_step].fields;
    const int i = fields.indexOf(f);
    if (i >= 0 && i + 1 < fields.size()) {
        fields[i + 1]->edit()->setFocus(Qt::TabFocusReason);
    } else if (m_next->isEnabled()) {
        next();
    }
}

void GuidedForm::focusField(const QString &key)
{
    if (GuidedField *f = field(key))
        f->edit()->setFocus(Qt::MouseFocusReason);    // like a click: puts the cursor in, selects nothing
}

void GuidedForm::goToStep(int index)
{
    m_step = qBound(0, index, int(m_steps.size()) - 1);
    m_finished = false;
    m_stack->setCurrentIndex(0);
    m_pages->setCurrentIndex(m_step);
    const Step &st = m_steps[m_step];
    m_eyebrow->setText(QString("STEP %1 OF %2").arg(m_step + 1).arg(m_steps.size()));
    m_heading->setText(st.heading);
    m_blurb->setText(st.blurb);
    m_next->setText(m_step == m_steps.size() - 1 ? "Create account" : "Continue");
    m_back->setEnabled(m_step > 0);

    m_active = nullptr;
    for (GuidedField *g : st.fields)
        g->setDimmed(false);
    updateAll();
    updateCoach(nullptr);

    // jump straight to the first field that still needs attention
    GuidedField *target = st.fields.first();
    for (GuidedField *g : st.fields)
        if (!g->isDone() && !g->spec().optional) { target = g; break; }
    target->edit()->setFocus(Qt::TabFocusReason);
}

void GuidedForm::next()
{
    if (!stepComplete(m_step))
        return;
    if (m_step + 1 < m_steps.size())
        goToStep(m_step + 1);
    else
        showSuccess();
}

void GuidedForm::back()
{
    if (m_step > 0)
        goToStep(m_step - 1);
}

void GuidedForm::showSuccess()
{
    const QString first = m_byKey["first"]->text(), last = m_byKey["last"]->text();
    m_sumTitle->setText(QString("You're all set, %1").arg(first.isEmpty() ? "friend" : first));
    m_sumValues[0]->setText((first + " " + last).trimmed());
    m_sumValues[1]->setText(m_byKey["email"]->text());
    m_sumValues[2]->setText(m_byKey["role"]->text() + "  ·  " + m_byKey["company"]->text());
    m_sumValues[3]->setText("@" + m_byKey["username"]->text());
    m_active = nullptr;
    m_finished = true;
    m_stack->setCurrentIndex(1);
    updateAll();

    // the guide has nothing left to explain: say so instead of showing the last field's help
    updateCoach(nullptr);
    m_coachTitle->setText("Nice work");
    m_coachText->setText("Every field checks out. You can change any of this later in Settings.");
}

void GuidedForm::reset()
{
    for (const Step &s : m_steps)
        for (GuidedField *f : s.fields)
            f->setText(QString());
    goToStep(0);
}
