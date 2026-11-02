#include "designs.h"
#include "guidedform.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QKeyEvent>
#include <QLineEdit>
#include <QTextStream>
#include <QTimer>
#include <QWidget>

// ./Showcase                       -> opens the Aurora design
// ./Showcase --design vault        -> aurora | vault | control | form
// ./Showcase --shots out_dir       -> renders PNGs of the three dashboards (use QT_QPA_PLATFORM=offscreen)
// ./Showcase --form-shots out_dir  -> walks the guided form through several states and renders each
// ./Showcase --form-animated dir   -> same form with real animations + a simulated Enter key press

static QWidget *create(const QString &name)
{
    if (name == "vault")   return makeVault();
    if (name == "control") return makeControlRoom();
    if (name == "form")    return new GuidedForm;
    return makeAurora();
}

static void save(QWidget *w, const QString &path)
{
    QApplication::processEvents();
    QApplication::processEvents();
    QTextStream(stdout) << (w->grab().save(path) ? "saved " : "FAILED ") << path << "\n";
}

// Drives the real form through realistic states, exactly as a user would: focus a field, type, move on.
static void formShots(const QString &dir)
{
    QDir().mkpath(dir);
    GuidedField::setAnimationsEnabled(false);          // jump straight to the end state of every animation

    GuidedForm w;
    w.show();
    w.activateWindow();
    QApplication::processEvents();

    // 1. first visit: cursor in the first field, everything else faded, guide explains it
    w.focusField("first");
    save(&w, dir + "/form_1_start.png");

    // 2. a typo in the email, then the cursor comes back to it: red glow, red checklist item
    w.field("first")->setText("Ada");
    w.focusField("last");
    w.field("last")->setText("Lovelace");
    w.focusField("email");
    w.field("email")->setText("ada@lovelace");
    w.focusField("phone");                             // leaving the field is what reveals the error
    w.focusField("email");
    save(&w, dir + "/form_2_error.png");

    // 3. fixed: every field valid, Continue lights up
    w.field("email")->setText("ada@lovelace.dev");
    w.focusField("phone");
    w.field("phone")->setText("+216 20 123 456");
    save(&w, dir + "/form_3_valid.png");

    // 4. last step: password half way through its checklist
    w.field("role")->setText("Mathematician");
    w.field("company")->setText("Analytical Engines Ltd");
    w.field("website")->setText("analytical.dev");
    w.goToStep(2);
    w.focusField("username");
    w.field("username")->setText("ada_l");
    w.focusField("password");
    w.field("password")->setText("Analytical1");
    save(&w, dir + "/form_4_password.png");

    // 5. done
    w.field("password")->setText("Analytical#1843");
    w.field("confirm")->setText("Analytical#1843");
    w.showSuccess();
    save(&w, dir + "/form_5_success.png");
}

static void wait(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// Same form, but with the REAL animations switched on and a real Enter key press: proves that the glow and
// fade animations reach their final state and that Enter moves the cursor to the next field.
static void formAnimated(const QString &dir)
{
    QDir().mkpath(dir);
    GuidedForm w;
    w.show();
    w.activateWindow();
    wait(100);
    w.focusField("first");
    w.field("first")->edit()->insert("Ada");

    QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(w.field("first")->edit(), &press);
    QApplication::sendEvent(w.field("first")->edit(), &release);
    wait(800);                                           // let every animation finish

    const bool ok = w.focusWidget() == w.field("last")->edit();
    QTextStream(stdout) << "Enter moved focus to the next field: " << (ok ? "YES" : "NO") << "\n";
    save(&w, dir + "/form_animated_after_enter.png");
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    const QStringList args = app.arguments();

    const int ai = args.indexOf("--form-animated");
    if (ai >= 0 && ai + 1 < args.size()) {
        formAnimated(args.at(ai + 1));
        return 0;
    }

    const int fi = args.indexOf("--form-shots");
    if (fi >= 0 && fi + 1 < args.size()) {
        formShots(args.at(fi + 1));
        return 0;
    }

    const int si = args.indexOf("--shots");
    if (si >= 0 && si + 1 < args.size()) {
        const QString dir = args.at(si + 1);
        QDir().mkpath(dir);
        QTextStream out(stdout);
        for (const QString n : {"aurora", "vault", "control"}) {
            QWidget *w = create(n);
            w->show();
            app.processEvents();
            const QString path = dir + "/" + n + ".png";
            out << (w->grab().save(path) ? "saved " : "FAILED ") << path << "\n";
            delete w;
        }
        return 0;
    }

    const int di = args.indexOf("--design");
    QWidget *w = create(di >= 0 && di + 1 < args.size() ? args.at(di + 1) : "aurora");
    w->show();
    return app.exec();
}
