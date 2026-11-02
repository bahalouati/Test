#pragma once

// Building blocks for the "guided focus" form (see guidedform.h for the window that uses them).
//
//   GuidedField   a label + line edit + status icon + message. It glows when active, fades when
//                 another field is active, and validates itself against a list of Rules.
//   ProgressRing  animated circular percentage.
//   StepList      vertical timeline of the form steps (done / current / upcoming).
//   PreviewCard   a profile card that fills in live while the user types.

#include <QColor>
#include <QFrame>
#include <QMap>
#include <QString>
#include <QVector>
#include <functional>

class QGraphicsDropShadowEffect;
class QGraphicsOpacityEffect;
class QLabel;
class QLineEdit;
class QPropertyAnimation;
class QPushButton;

// Colors used by the painted widgets (the .qss file uses the same values for the rest of the UI).
namespace GP {
inline const QColor accent{"#0f9d76"};
inline const QColor accentDark{"#0b7d5d"};
inline const QColor error{"#e0475b"};
inline const QColor ink{"#14211d"};
inline const QColor muted{"#6b7a74"};
inline const QColor line{"#dfe3de"};
inline const QColor faint{"#eceee9"};
}

// One requirement a value must satisfy. Shown to the user as a live checklist.
struct Rule {
    QString text;                                  // checklist wording:  "Contains a number"
    QString error;                                 // message under the field when this rule fails
    std::function<bool(const QString &)> ok;       // the actual test
};

// Everything the form needs to know about one field.
struct FieldSpec {
    QString key;           // stable id, e.g. "email"
    QString label;         // "Work email"
    QString placeholder;   // "ada@company.com"
    QString help;          // sentence shown in the coach panel while the field is active
    QString example;       // example value shown in the coach panel
    bool optional = false; // optional + empty counts as valid
    bool password = false; // hide characters and show a Show/Hide button
    QVector<Rule> rules;
};

// ---------------------------------------------------------------- tiny round status icon
class StatusIcon : public QFrame
{
    Q_OBJECT
public:
    explicit StatusIcon(QWidget *parent = nullptr);
    void setState(int s) { m_state = s; update(); }   // 0 none, 1 valid (check), 2 error (!)
protected:
    void paintEvent(QPaintEvent *) override;
private:
    int m_state = 0;
};

// ---------------------------------------------------------------- the guided field
class GuidedField : public QFrame
{
    Q_OBJECT
    Q_PROPERTY(qreal glowLevel READ glowLevel WRITE setGlowLevel)   // 0..1, animated
public:
    // Horizontal room kept free on each side of the input box so the painted glow is not clipped.
    // Whoever lays the fields out should shrink its own side margins by this amount.
    static constexpr int glowMargin = 14;

    explicit GuidedField(const FieldSpec &spec, QWidget *parent = nullptr);
    qreal glowLevel() const { return m_glowLevel; }
    void setGlowLevel(qreal v) { m_glowLevel = v; update(); }

    static void setAnimationsEnabled(bool on);   // turn off for screenshots / tests

    const FieldSpec &spec() const { return m_spec; }
    QLineEdit *edit() const { return m_edit; }
    QString text() const;
    void setText(const QString &t);

    bool isValid() const;                 // passes all rules (optional + empty also passes)
    bool isDone() const;                  // valid AND not empty (counts toward progress)
    QVector<bool> ruleStates() const;     // one entry per rule, for the checklist
    bool hasError() const { return m_showError; }

    void setDimmed(bool dimmed);          // fade out when another field is the focus
    void refresh();                       // recompute visual state (call after related fields change)

signals:
    void activated(GuidedField *self);    // the line edit received focus
    void edited(GuidedField *self);       // text changed
    void submitted(GuidedField *self);    // Return pressed

protected:
    bool eventFilter(QObject *o, QEvent *e) override;
    void paintEvent(QPaintEvent *e) override;     // paints the glow behind the input box

private:
    FieldSpec m_spec;
    QLabel *m_label = nullptr;
    QFrame *m_wrap = nullptr;             // rounded box that holds the edit and the icon (styled by QSS)
    QLineEdit *m_edit = nullptr;
    StatusIcon *m_icon = nullptr;
    QLabel *m_msg = nullptr;
    QGraphicsOpacityEffect *m_fade = nullptr;     // the ONLY graphics effect: nesting two breaks painting
    QPropertyAnimation *m_glowAnim = nullptr;
    QPropertyAnimation *m_fadeAnim = nullptr;
    qreal m_glowLevel = 0;
    QColor m_glowColor{GP::accent};
    bool m_focused = false;
    bool m_touched = false;               // has the user left the field at least once?
    bool m_showError = false;
};

// ---------------------------------------------------------------- progress ring
class ProgressRing : public QFrame
{
    Q_OBJECT
    Q_PROPERTY(qreal shown READ shown WRITE setShown)
public:
    explicit ProgressRing(QWidget *parent = nullptr);
    void setPercent(int p);               // animates to the new value
    qreal shown() const { return m_shown; }
    void setShown(qreal v) { m_shown = v; update(); }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    qreal m_shown = 0;
    QPropertyAnimation *m_anim = nullptr;
};

// ---------------------------------------------------------------- vertical step timeline
class StepList : public QFrame
{
    Q_OBJECT
public:
    struct Step { QString title; QString subtitle; bool done; };
    explicit StepList(QWidget *parent = nullptr);
    void setSteps(const QVector<Step> &s, int current) { m_steps = s; m_current = current; update(); }
signals:
    void stepClicked(int index);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
private:
    QVector<Step> m_steps;
    int m_current = 0;
    static constexpr int kRow = 92;
};

// ---------------------------------------------------------------- live preview card
class PreviewCard : public QFrame
{
    Q_OBJECT
public:
    explicit PreviewCard(QWidget *parent = nullptr);
    void setValue(const QString &key, const QString &text) { m_vals[key] = text.trimmed(); update(); }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QMap<QString, QString> m_vals;
};
