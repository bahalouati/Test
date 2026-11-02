#include "form_widgets.h"

#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>
#include <QFontMetrics>

namespace {

bool g_animations = true;

void repolish(QWidget *w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

QColor alpha(QColor c, int a) { c.setAlpha(a); return c; }

} // namespace

// ============================================================ StatusIcon
StatusIcon::StatusIcon(QWidget *parent) : QFrame(parent) { setFixedSize(22, 22); }

void StatusIcon::paintEvent(QPaintEvent *)
{
    if (m_state == 0)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor c = m_state == 1 ? GP::accent : GP::error;
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(rect().adjusted(1, 1, -1, -1));
    p.setPen(QPen(Qt::white, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (m_state == 1) {                       // check mark
        p.drawPolyline(QPolygonF{QPointF(6.5, 11.5), QPointF(9.7, 14.7), QPointF(15.5, 7.8)});
    } else {                                  // exclamation mark
        p.drawLine(QPointF(11, 6), QPointF(11, 12));
        p.drawPoint(QPointF(11, 15.6));
    }
}

// ============================================================ GuidedField
void GuidedField::setAnimationsEnabled(bool on) { g_animations = on; }

GuidedField::GuidedField(const FieldSpec &spec, QWidget *parent) : QFrame(parent), m_spec(spec)
{
    setObjectName("field");
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(glowMargin, 0, glowMargin, 0);
    col->setSpacing(6);

    m_label = new QLabel(spec.optional ? spec.label + "  <span style='color:#98a39e;font-weight:500'>optional</span>"
                                       : spec.label);
    m_label->setObjectName("fieldLabel");
    m_label->setTextFormat(Qt::RichText);
    col->addWidget(m_label);

    // The rounded "input box" is a QFrame so that QSS can draw the border/glow; the QLineEdit inside is borderless.
    m_wrap = new QFrame;
    m_wrap->setObjectName("inputWrap");
    m_wrap->setProperty("state", "idle");
    auto *row = new QHBoxLayout(m_wrap);
    row->setContentsMargins(14, 0, 12, 0);
    row->setSpacing(8);

    m_edit = new QLineEdit;
    m_edit->setObjectName("fieldEdit");
    m_edit->setPlaceholderText(spec.placeholder);
    m_edit->setFixedHeight(46);
    if (spec.password)
        m_edit->setEchoMode(QLineEdit::Password);
    m_edit->installEventFilter(this);
    row->addWidget(m_edit, 1);

    if (spec.password) {
        auto *eye = new QPushButton("Show");
        eye->setObjectName("showBtn");
        eye->setCheckable(true);
        eye->setCursor(Qt::PointingHandCursor);
        connect(eye, &QPushButton::toggled, this, [this, eye](bool on) {
            m_edit->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
            eye->setText(on ? "Hide" : "Show");
        });
        row->addWidget(eye);
    }
    m_icon = new StatusIcon;
    row->addWidget(m_icon);
    col->addWidget(m_wrap);

    m_msg = new QLabel;
    m_msg->setObjectName("fieldMsg");
    m_msg->setProperty("kind", "none");
    m_msg->setWordWrap(true);
    m_msg->setFixedHeight(18);          // reserve the space so the form does not jump when a message appears
    col->addWidget(m_msg);

    // Fade = opacity effect on the whole field. The glow is NOT a second graphics effect on the input box:
    // Qt cannot render an effect nested inside another effect (the box vanishes or shifts), so the glow is
    // painted by paintEvent() below and animated through the "glowLevel" property instead.
    m_fade = new QGraphicsOpacityEffect(this);
    m_fade->setOpacity(1.0);
    setGraphicsEffect(m_fade);

    m_glowAnim = new QPropertyAnimation(this, "glowLevel", this);
    m_glowAnim->setDuration(220);
    m_fadeAnim = new QPropertyAnimation(m_fade, "opacity", this);
    m_fadeAnim->setDuration(260);

    connect(m_edit, &QLineEdit::textChanged, this, [this] {
        refresh();
        emit edited(this);
    });
    connect(m_edit, &QLineEdit::returnPressed, this, [this] { emit submitted(this); });
    refresh();
}

QString GuidedField::text() const { return m_edit->text().trimmed(); }
void GuidedField::setText(const QString &t) { m_edit->setText(t); }

QVector<bool> GuidedField::ruleStates() const
{
    QVector<bool> v;
    for (const Rule &r : m_spec.rules)
        v.push_back(!text().isEmpty() && r.ok(text()));
    return v;
}

bool GuidedField::isValid() const
{
    if (text().isEmpty())
        return m_spec.optional;
    for (const Rule &r : m_spec.rules)
        if (!r.ok(text()))
            return false;
    return true;
}

bool GuidedField::isDone() const { return !text().isEmpty() && isValid(); }

void GuidedField::setDimmed(bool dimmed)
{
    const qreal target = dimmed ? 0.5 : 1.0;
    m_fadeAnim->stop();
    if (!g_animations) { m_fade->setOpacity(target); return; }
    m_fadeAnim->setStartValue(m_fade->opacity());
    m_fadeAnim->setEndValue(target);
    m_fadeAnim->start();
}

void GuidedField::refresh()
{
    const bool empty = text().isEmpty();
    const bool valid = isValid();

    // Errors are only shown after the user has typed something and either left the field or the value is
    // clearly past a point of no return - this avoids shouting at someone who has just started typing.
    QString err;
    if (!empty) {
        for (const Rule &r : m_spec.rules)
            if (!r.ok(text())) { err = r.error; break; }
    }
    m_showError = !empty && !valid && (m_touched || m_focused == false);

    QString state = "idle";
    if (m_showError)                 state = "error";
    else if (m_focused)              state = "active";
    else if (!empty && valid)        state = "valid";

    m_wrap->setProperty("state", state);
    repolish(m_wrap);

    m_icon->setState(m_showError ? 2 : (!empty && valid ? 1 : 0));

    if (m_showError) {
        m_msg->setText(err);
        m_msg->setProperty("kind", "error");
    } else if (!empty && valid) {
        m_msg->setText("Looks good");
        m_msg->setProperty("kind", "ok");
    } else {
        m_msg->setText(QString());
        m_msg->setProperty("kind", "none");
    }
    repolish(m_msg);

    // glow only while active (green normally, red while the field shows an error)
    m_glowColor = m_showError ? GP::error : GP::accent;
    m_glowAnim->stop();
    const qreal target = m_focused ? 1.0 : 0.0;
    if (!g_animations) { setGlowLevel(target); return; }
    m_glowAnim->setStartValue(m_glowLevel);
    m_glowAnim->setEndValue(target);
    m_glowAnim->start();
}

void GuidedField::paintEvent(QPaintEvent *e)
{
    QFrame::paintEvent(e);
    if (m_glowLevel < 0.01)
        return;
    // A soft halo: many filled rounded rectangles, each a pixel larger and a little more transparent than the
    // last. Painted before the children, so the opaque input box covers the inner part and only the glow
    // outside its border stays visible.
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    const QRectF r = m_wrap->geometry();
    const int layers = glowMargin;
    for (int i = layers; i >= 1; --i) {
        const qreal t = 1.0 - qreal(i) / layers;               // 0 at the outer edge, close to 1 at the box
        QColor c = m_glowColor;
        c.setAlphaF(qBound(0.0, 0.11 * t * t * m_glowLevel, 1.0));
        p.setBrush(c);
        p.drawRoundedRect(r.adjusted(-i, -i, i, i), 14 + i, 14 + i);
    }
}

bool GuidedField::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_edit) {
        if (e->type() == QEvent::FocusIn) {
            m_focused = true;
            // "Reward early, punish late": if the value is fine when you come back, do not flag errors
            // until you leave again. If it was already showing an error, keep it live while you fix it.
            if (isValid())
                m_touched = false;
            refresh();
            emit activated(this);
        } else if (e->type() == QEvent::FocusOut) {
            m_focused = false;
            m_touched = !text().isEmpty();
            refresh();
        }
    }
    return QFrame::eventFilter(o, e);
}

// ============================================================ ProgressRing
ProgressRing::ProgressRing(QWidget *parent) : QFrame(parent)
{
    setFixedSize(92, 92);
    m_anim = new QPropertyAnimation(this, "shown", this);
    m_anim->setDuration(450);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
}

void ProgressRing::setPercent(int p)
{
    m_anim->stop();
    if (!g_animations) { setShown(p); return; }
    m_anim->setStartValue(m_shown);
    m_anim->setEndValue(qreal(p));
    m_anim->start();
}

void ProgressRing::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(8, 8, -8, -8);
    p.setPen(QPen(GP::faint, 8, Qt::SolidLine, Qt::RoundCap));
    p.drawEllipse(r);
    if (m_shown > 0.5) {
        p.setPen(QPen(GP::accent, 8, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(r, 90 * 16, int(-360 * 16 * m_shown / 100.0));
    }
    QFont f = font();
    f.setPixelSize(21);
    f.setWeight(QFont::Bold);
    p.setFont(f);
    p.setPen(GP::ink);
    p.drawText(rect(), Qt::AlignCenter, QString::number(qRound(m_shown)) + "%");
}

// ============================================================ StepList
StepList::StepList(QWidget *parent) : QFrame(parent)
{
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(kRow * 3);
}

void StepList::mousePressEvent(QMouseEvent *e)
{
    const int i = int(e->position().y()) / kRow;
    if (i >= 0 && i < m_steps.size())
        emit stepClicked(i);
}

void StepList::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal cx = 20;
    for (int i = 0; i < m_steps.size(); ++i) {
        const qreal cy = i * kRow + 24;
        const bool done = m_steps[i].done;
        const bool cur = i == m_current;

        if (i + 1 < m_steps.size()) {                       // connector to the next step
            p.setPen(QPen(done ? GP::accent : GP::line, 2.5, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(cx, cy + 20), QPointF(cx, cy + kRow - 20));
        }
        if (cur) {                                          // soft halo behind the current step
            p.setPen(Qt::NoPen);
            p.setBrush(alpha(GP::accent, 38));
            p.drawEllipse(QPointF(cx, cy), 21, 21);
        }
        p.setPen(done || cur ? Qt::NoPen : QPen(GP::line, 2));
        p.setBrush(done ? GP::accent : (cur ? GP::accent : Qt::white));
        p.drawEllipse(QPointF(cx, cy), 14, 14);

        p.setPen(QPen(done || cur ? Qt::white : GP::muted, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (done) {
            p.drawPolyline(QPolygonF{QPointF(cx - 5.5, cy + 0.5), QPointF(cx - 1.5, cy + 4.5), QPointF(cx + 5.5, cy - 3.5)});
        } else {
            QFont nf = font();
            nf.setPixelSize(13);
            nf.setWeight(QFont::Bold);
            p.setFont(nf);
            p.drawText(QRectF(cx - 14, cy - 14, 28, 28), Qt::AlignCenter, QString::number(i + 1));
        }

        QFont tf = font();
        tf.setPixelSize(15);
        tf.setWeight(cur ? QFont::Bold : QFont::DemiBold);
        p.setFont(tf);
        p.setPen(cur || done ? GP::ink : GP::muted);
        p.drawText(QRectF(52, cy - 14, width() - 56, 20), Qt::AlignLeft | Qt::AlignVCenter, m_steps[i].title);

        QFont sf = font();
        sf.setPixelSize(12);
        p.setFont(sf);
        p.setPen(GP::muted);
        p.drawText(QRectF(52, cy + 6, width() - 56, 18), Qt::AlignLeft | Qt::AlignVCenter, m_steps[i].subtitle);
    }
}

// ============================================================ PreviewCard
PreviewCard::PreviewCard(QWidget *parent) : QFrame(parent)
{
    setFixedHeight(190);
}

void PreviewCard::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);

    QPainterPath clip;
    clip.addRoundedRect(r, 18, 18);
    QLinearGradient g(r.topLeft(), r.bottomRight());
    g.setColorAt(0, QColor("#0e2a24"));
    g.setColorAt(1, QColor("#14513f"));
    p.fillPath(clip, g);

    p.setClipPath(clip);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 14));
    p.drawEllipse(QPointF(r.right() - 20, r.top() + 10), 120, 120);
    p.setClipping(false);

    const QString first = m_vals.value("first"), last = m_vals.value("last");
    const QString full = (first + " " + last).trimmed();
    const QString role = m_vals.value("role"), company = m_vals.value("company");
    const QString email = m_vals.value("email"), phone = m_vals.value("phone");

    // avatar with initials
    const QPointF c(r.left() + 46, r.top() + 50);
    p.setBrush(alpha(Qt::white, 36));
    p.drawEllipse(c, 28, 28);
    QString initials;
    if (!first.isEmpty()) initials += first[0].toUpper();
    if (!last.isEmpty())  initials += last[0].toUpper();
    QFont f = font();
    f.setPixelSize(20);
    f.setWeight(QFont::Bold);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRectF(c.x() - 28, c.y() - 28, 56, 56), Qt::AlignCenter, initials.isEmpty() ? QString::fromUtf8("•") : initials);

    auto line = [&](const QString &text, const QString &placeholder, qreal y, int px, int weight, qreal alphaV) {
        QFont lf = font();
        lf.setPixelSize(px);
        lf.setWeight(QFont::Weight(weight));
        p.setFont(lf);
        const bool empty = text.isEmpty();
        QColor col = Qt::white;
        col.setAlphaF(empty ? 0.34 : alphaV);
        p.setPen(col);
        const QString t = QFontMetrics(lf).elidedText(empty ? placeholder : text, Qt::ElideRight, int(r.width() - 100 - 20));
        p.drawText(QPointF(r.left() + 88, y), t);
    };
    line(full, "Your name", r.top() + 46, 19, QFont::Bold, 1.0);
    line(role.isEmpty() && company.isEmpty() ? QString()
         : (role.isEmpty() ? company : (company.isEmpty() ? role : role + "  ·  " + company)),
         "Role · Company", r.top() + 68, 13, QFont::DemiBold, 0.80);

    // divider
    p.setPen(QPen(QColor(255, 255, 255, 40), 1));
    p.drawLine(QPointF(r.left() + 24, r.top() + 104), QPointF(r.right() - 24, r.top() + 104));

    auto info = [&](const QString &tag, const QString &text, const QString &placeholder, qreal y) {
        QFont tf = font();
        tf.setPixelSize(10);
        tf.setWeight(QFont::Bold);
        tf.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
        p.setFont(tf);
        p.setPen(QColor(255, 255, 255, 110));
        p.drawText(QPointF(r.left() + 24, y), tag);
        QFont vf = font();
        vf.setPixelSize(13);
        vf.setWeight(QFont::DemiBold);
        p.setFont(vf);
        const bool empty = text.isEmpty();
        p.setPen(QColor(255, 255, 255, empty ? 86 : 235));
        p.drawText(QPointF(r.left() + 82, y), QFontMetrics(vf).elidedText(empty ? placeholder : text, Qt::ElideRight, int(r.width() - 110)));
    };
    info("EMAIL", email, "name@company.com", r.top() + 132);
    info("PHONE", phone, "+216 00 000 000", r.top() + 158);
}
