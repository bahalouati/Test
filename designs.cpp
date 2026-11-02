#include "designs.h"
#include "widgets.h"
#include "qssloader.h"

#include <QButtonGroup>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

// ------------------------------------------------------------------ small construction helpers
QLabel *lbl(const QString &text, const char *obj = nullptr)
{
    auto *l = new QLabel(text);
    if (obj) l->setObjectName(obj);
    return l;
}

QFrame *frame(const char *obj, QVBoxLayout *&lay, int l = 20, int t = 18, int r = 20, int b = 18, int sp = 12)
{
    auto *f = new QFrame;
    f->setObjectName(obj);
    lay = new QVBoxLayout(f);
    lay->setContentsMargins(l, t, r, b);
    lay->setSpacing(sp);
    return f;
}

QPushButton *pbtn(const QString &text, const char *obj = nullptr)
{
    auto *b = new QPushButton(text);
    if (obj) b->setObjectName(obj);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QFrame *segmented(const QStringList &items, int current)
{
    auto *f = new QFrame;
    f->setObjectName("seg");
    auto *h = new QHBoxLayout(f);
    h->setContentsMargins(4, 4, 4, 4);
    h->setSpacing(2);
    auto *g = new QButtonGroup(f);
    for (int i = 0; i < items.size(); ++i) {
        auto *b = pbtn(items[i], "segBtn");
        b->setCheckable(true);
        b->setChecked(i == current);
        g->addButton(b);
        h->addWidget(b);
    }
    return f;
}

QLabel *chip(const QString &text, const char *obj)
{
    auto *l = lbl(text, obj);
    l->setAlignment(Qt::AlignCenter);
    l->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    return l;
}

QWidget *cellChip(const QString &text, const char *obj)
{
    auto *w = new QWidget;
    w->setObjectName("cell");
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(8, 0, 0, 0);
    h->addWidget(chip(text, obj));
    h->addStretch();
    return w;
}

QTableWidget *makeTable(const QStringList &headers, int rows, int rowHeight = 46)
{
    auto *t = new QTableWidget(rows, headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->setVisible(false);
    t->verticalHeader()->setDefaultSectionSize(rowHeight);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->horizontalHeader()->setHighlightSections(false);
    t->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    t->setShowGrid(false);
    t->setSelectionMode(QAbstractItemView::NoSelection);
    t->setFocusPolicy(Qt::NoFocus);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    t->setFrameShape(QFrame::NoFrame);
    return t;
}

void setCell(QTableWidget *t, int r, int c, const QString &text, Qt::Alignment a = Qt::AlignLeft | Qt::AlignVCenter)
{
    auto *it = new QTableWidgetItem(text);
    it->setTextAlignment(a);
    t->setItem(r, c, it);
}

void softShadow(QWidget *w, int blur = 40, int dy = 10, int alpha = 28)
{
    auto *fx = new QGraphicsDropShadowEffect(w);
    fx->setBlurRadius(blur);
    fx->setOffset(0, dy);
    fx->setColor(QColor(30, 40, 90, alpha));
    w->setGraphicsEffect(fx);
}

QWidget *makeWindow(const QString &qss)
{
    auto *w = new QWidget;
    w->setObjectName("root");
    w->setWindowTitle("Showcase");
    w->resize(designSize());
    w->setStyleSheet(qss);
    return w;
}

} // namespace

// =====================================================================================
//  1) AURORA - dark analytics dashboard
// =====================================================================================

QWidget *makeAurora()
{
    QWidget *root = makeWindow(loadQss("aurora"));
    auto *outer = new QHBoxLayout(root);
    outer->setContentsMargins(22, 22, 26, 22);
    outer->setSpacing(22);

    // ---- floating icon rail
    auto *rail = new QFrame;
    rail->setObjectName("rail");
    rail->setFixedWidth(76);
    auto *rl = new QVBoxLayout(rail);
    rl->setContentsMargins(14, 14, 14, 14);
    rl->setSpacing(8);
    auto *logo = lbl("A", "logo");
    logo->setAlignment(Qt::AlignCenter);
    logo->setFixedSize(48, 48);
    rl->addWidget(logo);
    rl->addSpacing(14);
    auto *grp = new QButtonGroup(rail);
    const QStringList icons = {QString::fromUtf8("▦"), QString::fromUtf8("↗"), QString::fromUtf8("☺"),
                               QString::fromUtf8("✉"), QString::fromUtf8("⚙")};
    for (int i = 0; i < icons.size(); ++i) {
        auto *b = pbtn(icons[i], "railBtn");
        b->setCheckable(true);
        b->setChecked(i == 0);
        grp->addButton(b);
        rl->addWidget(b);
    }
    rl->addStretch();
    outer->addWidget(rail);

    // ---- main column
    auto *col = new QVBoxLayout;
    col->setSpacing(18);
    outer->addLayout(col, 1);

    // header
    auto *head = new QHBoxLayout;
    auto *titles = new QVBoxLayout;
    titles->setSpacing(2);
    titles->addWidget(lbl("Good evening, Ada", "h1"));
    titles->addWidget(lbl("Here is what happened across Nimbus today  ·  Sat 31 Oct 2026", "sub"));
    head->addLayout(titles);
    head->addStretch();
    head->addWidget(segmented({"7D", "30D", "90D", "1Y"}, 1));
    auto *search = new QLineEdit;
    search->setObjectName("search");
    search->setPlaceholderText("Search customers, invoices...");
    search->setFixedWidth(250);
    head->addSpacing(8);
    head->addWidget(search);
    head->addWidget(pbtn("Export report", "cta"));
    col->addLayout(head);

    // KPI row
    auto *kpis = new QHBoxLayout;
    kpis->setSpacing(18);
    struct K { QString t, v, d; const char *dc; QVector<qreal> s; QColor c; };
    const QVector<K> ks = {
        {"Revenue",     "$61,240", "+12.4%", "up",   {20, 24, 22, 30, 28, 36, 41, 39, 48, 55, 61}, "#8b5cf6"},
        {"Active users", "8,942",  "+4.1%",  "up",   {30, 28, 33, 35, 34, 40, 38, 44, 46, 45, 52}, "#22d3ee"},
        {"Conversion",  "3.82%",   "-0.6%",  "down", {44, 46, 41, 43, 40, 42, 38, 39, 37, 38, 36}, "#f472b6"},
        {"Avg. session", "4m 12s", "+8.9%",  "up",   {10, 14, 13, 18, 17, 22, 21, 26, 25, 29, 33}, "#34d399"},
    };
    for (const auto &k : ks) {
        QVBoxLayout *kl = nullptr;
        QFrame *f = frame("kpi", kl, 20, 16, 20, 14, 6);
        auto *top = new QHBoxLayout;
        top->addWidget(lbl(k.t, "kpiTitle"));
        top->addStretch();
        top->addWidget(chip(k.d, k.dc));
        kl->addLayout(top);
        kl->addWidget(lbl(k.v, "kpiValue"));
        auto *sp = new Sparkline;
        sp->setData(k.s, k.c);
        sp->setFixedHeight(38);
        kl->addWidget(sp);
        kpis->addWidget(f);
    }
    col->addLayout(kpis);

    // chart row
    auto *mid = new QHBoxLayout;
    mid->setSpacing(18);

    QVBoxLayout *cl = nullptr;
    QFrame *chartCard = frame("card", cl, 22, 18, 22, 14, 8);
    auto *ch = new QHBoxLayout;
    auto *chT = new QVBoxLayout;
    chT->setSpacing(0);
    chT->addWidget(lbl("Revenue overview", "h2"));
    chT->addWidget(lbl("Monthly recurring revenue vs. previous year", "sub"));
    ch->addLayout(chT);
    ch->addStretch();
    ch->addWidget(chip("<span style='color:#a78bfa'>&#9679;</span>&nbsp;This year", "legend"));
    ch->addSpacing(6);
    ch->addWidget(chip("<span style='color:#566083'>&#9679;</span>&nbsp;Last year", "legend"));
    cl->addLayout(ch);
    // values in dollars (thousands scaled up so the axis shows 20k, 40k, ...)
    QVector<qreal> a, b2;
    for (qreal v : {14, 16, 15, 19, 21, 20, 24, 26, 25, 29, 31, 33}) a << v * 1000;
    for (qreal v : {18, 22, 21, 28, 31, 29, 38, 42, 40, 49, 54, 61}) b2 << v * 1000;
    auto *line = new LineChart;
    line->addSeries(a, QColor("#566083"), false);
    line->addSeries(b2, QColor("#a78bfa"), true);
    line->setLabels({"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"});
    line->setTheme(QColor(255, 255, 255, 22), QColor("#7d88a8"), QColor("#ffffff"), QColor("#0d1224"));
    line->setValueFormat("$", "");
    cl->addWidget(line, 1);
    mid->addWidget(chartCard, 5);

    QVBoxLayout *dl = nullptr;
    QFrame *donutCard = frame("card", dl, 22, 18, 22, 18, 8);
    dl->addWidget(lbl("Traffic sources", "h2"));
    auto *donut = new DonutChart;
    donut->setSlices({{42, QColor("#8b5cf6")}, {28, QColor("#22d3ee")}, {18, QColor("#f472b6")}, {12, QColor("#34d399")}});
    donut->setCenter("128k", "visitors", QColor("#ffffff"), QColor("#8892b0"));
    donut->setMinimumHeight(150);
    dl->addWidget(donut, 1);
    const QVector<QPair<QString, QPair<QString, QColor>>> leg = {
        {"Organic search", {"42%", "#8b5cf6"}}, {"Direct", {"28%", "#22d3ee"}},
        {"Social", {"18%", "#f472b6"}},         {"Referral", {"12%", "#34d399"}}};
    for (const auto &e : leg) {
        auto *row = new QHBoxLayout;
        row->setSpacing(10);
        row->addWidget(new StatusDot(e.second.second));
        row->addWidget(lbl(e.first, "legend"));
        row->addStretch();
        row->addWidget(lbl(e.second.first, "pct"));
        dl->addLayout(row);
    }
    mid->addWidget(donutCard, 2);
    col->addLayout(mid, 5);

    // bottom row
    auto *bot = new QHBoxLayout;
    bot->setSpacing(18);

    QVBoxLayout *tl = nullptr;
    QFrame *tableCard = frame("card", tl, 22, 18, 22, 10, 6);
    auto *th = new QHBoxLayout;
    th->addWidget(lbl("Top customers", "h2"));
    th->addStretch();
    th->addWidget(lbl("View all  ›", "sub"));
    tl->addLayout(th);
    auto *table = makeTable({"CUSTOMER", "PLAN", "MRR", "STATUS"}, 4, 40);
    const QStringList cust[] = {
        {"Northwind Labs", "Pro", "$4,820"}, {"Helix Systems", "Enterprise", "$12,400"},
        {"Orbital Studio", "Starter", "$290"}, {"Lumen Health", "Enterprise", "$9,980"}};
    const char *stObj[] = {"up", "up", "warn", "up"};
    const char *stTxt[] = {"Active", "Active", "Trial", "Active"};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 3; ++c) setCell(table, r, c, cust[r][c]);
        table->setCellWidget(r, 3, cellChip(stTxt[r], stObj[r]));
    }
    tl->addWidget(table, 1);
    bot->addWidget(tableCard, 5);

    QVBoxLayout *al = nullptr;
    QFrame *autoCard = frame("card", al, 22, 18, 22, 14, 10);
    al->addWidget(lbl("Automations", "h2"));
    struct A { QString n, d; bool on; };
    const QVector<A> as = {{"Weekly digest", "Mondays at 09:00", true}, {"Churn alerts", "Notify when risk > 70%", true},
                           {"Auto-invoicing", "Send on the 1st", true}, {"Slack mirror", "Post new signups", false}};
    for (const auto &x : as) {
        auto *row = new QHBoxLayout;
        auto *tx = new QVBoxLayout;
        tx->setSpacing(0);
        tx->addWidget(lbl(x.n, "autoName"));
        tx->addWidget(lbl(x.d, "sub"));
        row->addLayout(tx);
        row->addStretch();
        auto *t = new ToggleSwitch;
        t->setColors(QColor("#8b5cf6"), QColor(255, 255, 255, 40), QColor("#ffffff"));
        t->setCheckedInstant(x.on);
        row->addWidget(t);
        al->addLayout(row);
    }
    al->addStretch();
    bot->addWidget(autoCard, 2);
    col->addLayout(bot, 4);
    return root;
}

// =====================================================================================
//  2) VAULT - light fintech app
// =====================================================================================

QWidget *makeVault()
{
    QWidget *root = makeWindow(loadQss("vault"));
    auto *outer = new QHBoxLayout(root);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ---- sidebar
    auto *side = new QFrame;
    side->setObjectName("side");
    side->setFixedWidth(236);
    auto *sl = new QVBoxLayout(side);
    sl->setContentsMargins(20, 26, 20, 22);
    sl->setSpacing(4);
    sl->addWidget(lbl(QString::fromUtf8("◈  Vault"), "brand"));
    sl->addSpacing(22);
    auto *grp = new QButtonGroup(side);
    const QStringList nav = {"Overview", "Cards", "Transfers", "Savings", "Insights", "Settings"};
    for (int i = 0; i < nav.size(); ++i) {
        auto *b = pbtn(nav[i], "nav");
        b->setCheckable(true);
        b->setChecked(i == 0);
        grp->addButton(b);
        sl->addWidget(b);
    }
    sl->addStretch();
    QVBoxLayout *ul = nullptr;
    QFrame *up = frame("upgrade", ul, 18, 18, 18, 18, 8);
    auto *ut = lbl("Vault Plus", "h2");
    ul->addWidget(ut);
    auto *ud = lbl("Zero-fee transfers, 4.2% APY savings and premium cards.");
    ud->setWordWrap(true);
    ul->addWidget(ud);
    ul->addWidget(pbtn("Upgrade now", "light"));
    sl->addWidget(up);
    outer->addWidget(side);

    // ---- content
    auto *content = new QVBoxLayout;
    content->setContentsMargins(32, 26, 32, 26);
    content->setSpacing(20);
    outer->addLayout(content, 1);

    auto *head = new QHBoxLayout;
    auto *ht = new QVBoxLayout;
    ht->setSpacing(2);
    ht->addWidget(lbl("Overview", "h1"));
    ht->addWidget(lbl("Welcome back, Ada. Your finances look healthy today.", "sub"));
    head->addLayout(ht);
    head->addStretch();
    auto *search = new QLineEdit;
    search->setObjectName("search");
    search->setPlaceholderText("Search transactions...");
    search->setFixedWidth(260);
    head->addWidget(search);
    head->addSpacing(6);
    head->addWidget(pbtn(QString::fromUtf8("✉"), "bell"));
    head->addSpacing(6);
    auto *av = lbl("AL", "avatar");
    av->setAlignment(Qt::AlignCenter);
    av->setFixedSize(38, 38);
    head->addWidget(av);
    content->addLayout(head);

    auto *row1 = new QHBoxLayout;
    row1->setSpacing(20);

    // left: card + balance
    QVBoxLayout *ll = nullptr;
    QFrame *cardPanel = frame("card", ll, 24, 22, 24, 22, 14);
    softShadow(cardPanel, 36, 8, 18);
    auto *lh = new QHBoxLayout;
    lh->addWidget(lbl("My cards", "h2"));
    lh->addStretch();
    lh->addWidget(lbl("+ Add card", "muted"));
    ll->addLayout(lh);
    auto *card = new CreditCard;
    card->setup("4821  7390  1146  0925", "ADA LOVELACE", "08 / 29", QColor("#231f6b"), QColor("#6a5cff"), "VAULT");
    ll->addWidget(card, 0, Qt::AlignHCenter);
    auto *bh = new QHBoxLayout;
    auto *bt = new QVBoxLayout;
    bt->setSpacing(0);
    bt->addWidget(lbl("Total balance", "muted"));
    bt->addWidget(lbl("$24,860.50", "big"));
    bh->addLayout(bt);
    bh->addStretch();
    bh->addWidget(chip("▲ 2.4%", "up"), 0, Qt::AlignTop);
    ll->addLayout(bh);
    auto *acts = new QHBoxLayout;
    acts->setSpacing(10);
    acts->addWidget(pbtn(QString::fromUtf8("↗  Send"), "primary"));
    acts->addWidget(pbtn(QString::fromUtf8("↙  Request"), "action"));
    acts->addWidget(pbtn(QString::fromUtf8("＋  Top up"), "action"));
    ll->addLayout(acts);
    ll->addStretch(1);
    row1->addWidget(cardPanel, 4);

    // right: spending
    QVBoxLayout *sp = nullptr;
    QFrame *spendPanel = frame("card", sp, 24, 22, 24, 22, 10);
    softShadow(spendPanel, 36, 8, 18);
    auto *sh = new QHBoxLayout;
    auto *st = new QVBoxLayout;
    st->setSpacing(0);
    st->addWidget(lbl("Spending analytics", "h2"));
    st->addWidget(lbl("You spent $1,620 this week, 8% less than last week", "sub"));
    sh->addLayout(st);
    sh->addStretch();
    sh->addWidget(segmented({"Week", "Month", "Year"}, 0));
    sp->addLayout(sh);
    auto *bars = new BarChart;
    bars->setData({120, 240, 180, 420, 260, 90, 310}, {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"}, 3);
    bars->setTheme(QColor("#4a43e0"), QColor("#8d84ff"), QColor("#dcdff0"), QColor("#f3f4fb"), QColor("#8a90a8"),
                   QColor("#151a2e"), QColor("#ffffff"));
    bars->setValueFormat("$", "");
    bars->setMinimumHeight(190);
    sp->addWidget(bars, 1);

    struct C { QString n, v; int pct; QString col; };
    const QVector<C> cats = {{"Food & dining", "$820", 74, "#4a43e0"}, {"Shopping", "$540", 52, "#f59e0b"},
                             {"Transport", "$260", 30, "#10b981"}};
    for (const auto &c : cats) {
        auto *r = new QHBoxLayout;
        r->setSpacing(14);
        auto *n = lbl(c.n);
        n->setMinimumWidth(110);
        r->addWidget(n);
        auto *pb = new QProgressBar;
        pb->setTextVisible(false);
        pb->setValue(c.pct);
        pb->setStyleSheet(QString("QProgressBar::chunk { background: %1; }").arg(c.col));
        r->addWidget(pb, 1);
        auto *v = lbl(c.v, "muted");
        v->setMinimumWidth(48);
        v->setAlignment(Qt::AlignRight);
        r->addWidget(v);
        sp->addLayout(r);
    }
    row1->addWidget(spendPanel, 6);
    content->addLayout(row1, 6);

    // transactions
    QVBoxLayout *tl = nullptr;
    QFrame *txPanel = frame("card", tl, 24, 18, 24, 10, 6);
    softShadow(txPanel, 36, 8, 18);
    auto *th = new QHBoxLayout;
    th->addWidget(lbl("Recent transactions", "h2"));
    th->addStretch();
    th->addWidget(lbl("See all  ›", "muted"));
    tl->addLayout(th);
    auto *table = makeTable({"MERCHANT", "CATEGORY", "DATE", "STATUS", "AMOUNT"}, 3, 38);
    struct T { QString m, c, d, s, a; bool neg; };
    const QVector<T> tx = {
        {"Spotify Premium", "Subscriptions", "Oct 30, 2026", "Completed", "-$11.99", true},
        {"Acme Corp - Salary", "Income", "Oct 28, 2026", "Completed", "+$5,400.00", false},
        {"Uber Trip", "Transport", "Oct 27, 2026", "Pending", "-$18.40", true}};
    for (int r = 0; r < tx.size(); ++r) {
        setCell(table, r, 0, tx[r].m);
        setCell(table, r, 1, tx[r].c);
        setCell(table, r, 2, tx[r].d);
        table->setCellWidget(r, 3, cellChip(tx[r].s, tx[r].s == "Pending" ? "pend" : "up"));
        setCell(table, r, 4, tx[r].a, Qt::AlignRight | Qt::AlignVCenter);
        table->item(r, 4)->setForeground(QColor(tx[r].neg ? "#151a2e" : "#0e9f6e"));
        QFont f = table->item(r, 4)->font();
        f.setBold(true);
        table->item(r, 4)->setFont(f);
    }
    table->horizontalHeaderItem(4)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    tl->addWidget(table, 1);
    content->addWidget(txPanel, 3);
    return root;
}

// =====================================================================================
//  3) CONTROL ROOM - industrial HMI
// =====================================================================================

QWidget *makeControlRoom()
{
    QWidget *root = makeWindow(loadQss("control"));
    auto *outer = new QVBoxLayout(root);
    outer->setContentsMargins(22, 20, 22, 20);
    outer->setSpacing(16);

    // ---- top bar
    auto *top = new QFrame;
    top->setObjectName("top");
    auto *tl = new QHBoxLayout(top);
    tl->setContentsMargins(22, 14, 18, 14);
    tl->setSpacing(14);
    tl->addWidget(new StatusDot(QColor("#2dd4bf")));
    auto *tt = new QVBoxLayout;
    tt->setSpacing(0);
    tt->addWidget(lbl("PLANT 04  ·  COOLING LOOP B", "title"));
    tt->addWidget(lbl("Hall 2  /  Primary circuit  /  Operator: A. Lovelace", "crumb"));
    tl->addLayout(tt);
    tl->addStretch();
    tl->addWidget(lbl("11:48:32  UTC+1", "clock"));
    tl->addSpacing(10);
    tl->addWidget(chip("● ONLINE", "online"));
    tl->addSpacing(6);
    tl->addWidget(pbtn("Acknowledge", "ghost"));
    tl->addWidget(pbtn("E-STOP", "estop"));
    outer->addWidget(top);

    // ---- gauges
    QVBoxLayout *gl = nullptr;
    QFrame *gp = frame("panel", gl, 22, 16, 22, 12, 4);
    gl->addWidget(lbl("PROCESS VARIABLES", "h2"));
    auto *gr = new QHBoxLayout;
    gr->setSpacing(10);
    struct G { QString t, u; qreal v, m; QString c; };
    const QVector<G> gs = {{"Coolant temperature", "°C", 68.4, 120, "#2dd4bf"},
                           {"Line pressure", "bar", 6.8, 10, "#fbbf24"},
                           {"Flow rate", "L/min", 312, 500, "#2dd4bf"},
                           {"Pump load", "%", 91, 100, "#f87171"}};
    for (const auto &g : gs) {
        auto *a = new ArcGauge;
        a->setup(g.t, g.u, g.v, g.m, QColor(g.c), QColor("#14292b"), QColor("#e6fbf6"), QColor("#6f9a96"));
        a->setMinimumHeight(210);
        gr->addWidget(a);
    }
    gl->addLayout(gr);
    outer->addWidget(gp, 4);

    // ---- bottom row
    auto *bot = new QHBoxLayout;
    bot->setSpacing(16);

    QVBoxLayout *cl = nullptr;
    QFrame *chartP = frame("panel", cl, 22, 16, 22, 12, 6);
    auto *chh = new QHBoxLayout;
    chh->addWidget(lbl("LOOP THROUGHPUT  ·  LAST 12 H", "h2"));
    chh->addStretch();
    chh->addWidget(chip("<span style='color:#2dd4bf'>&#9679;</span>&nbsp;Actual", "legend"));
    chh->addSpacing(6);
    chh->addWidget(chip("<span style='color:#fbbf24'>&#9679;</span>&nbsp;Setpoint", "legend"));
    cl->addLayout(chh);
    auto *lc = new LineChart;
    lc->addSeries({310, 305, 318, 330, 322, 340, 352, 338, 331, 345, 329, 312}, QColor("#2dd4bf"), true);
    lc->addSeries({320, 320, 320, 335, 335, 335, 345, 345, 345, 330, 330, 330}, QColor("#fbbf24"), false);
    lc->setYRange(280, 380);
    lc->setLabels({"00h", "01h", "02h", "03h", "04h", "05h", "06h", "07h", "08h", "09h", "10h", "11h"});
    lc->setTheme(QColor(45, 212, 191, 26), QColor("#4f7a76"), QColor("#e6fbf6"), QColor("#06201c"));
    lc->setValueFormat("", " L");
    cl->addWidget(lc, 1);
    bot->addWidget(chartP, 4);

    QVBoxLayout *el = nullptr;
    QFrame *eqP = frame("panel", el, 22, 16, 22, 14, 10);
    el->addWidget(lbl("EQUIPMENT", "h2"));
    struct E { QString n, s, col; bool on; };
    const QVector<E> es = {{"Pump A", "Running  ·  1,450 rpm", "#2dd4bf", true},
                           {"Pump B", "Standby", "#4f7a76", false},
                           {"Valve V-12", "Open  ·  82%", "#2dd4bf", true},
                           {"Heater H1", "Fault: thermal trip", "#f87171", false},
                           {"Chiller C2", "Running  ·  6.1 kW", "#2dd4bf", true}};
    for (const auto &e : es) {
        auto *r = new QHBoxLayout;
        r->setSpacing(12);
        r->addWidget(new StatusDot(QColor(e.col)));
        auto *tx = new QVBoxLayout;
        tx->setSpacing(0);
        tx->addWidget(lbl(e.n, "equip"));
        tx->addWidget(lbl(e.s, "sub"));
        r->addLayout(tx);
        r->addStretch();
        auto *t = new ToggleSwitch;
        t->setColors(QColor("#14b8a6"), QColor("#1c3a3b"), QColor("#e6fbf6"));
        t->setCheckedInstant(e.on);
        r->addWidget(t);
        el->addLayout(r);
    }
    el->addStretch();
    bot->addWidget(eqP, 3);

    QVBoxLayout *al = nullptr;
    QFrame *alP = frame("panel", al, 22, 16, 22, 10, 6);
    al->addWidget(lbl("ACTIVE ALARMS", "h2"));
    auto *table = makeTable({"TIME", "LEVEL", "MESSAGE"}, 5, 38);
    struct A { QString t, l, m; const char *o; };
    const QVector<A> as = {{"11:42:07", "CRITICAL", "H1 thermal trip", "crit"},
                           {"11:30:55", "WARNING", "Pump A load > 90%", "warn"},
                           {"10:58:12", "WARNING", "Line pressure high", "warn"},
                           {"10:21:40", "INFO", "Filter F3 due in 6 days", "info"},
                           {"09:03:18", "INFO", "Setpoint changed by operator", "info"}};
    for (int r = 0; r < as.size(); ++r) {
        auto *it = new QTableWidgetItem(as[r].t);
        QFont mf("DejaVu Sans Mono");
        mf.setPixelSize(12);
        it->setFont(mf);
        it->setForeground(QColor("#9bd9d0"));
        table->setItem(r, 0, it);
        table->setCellWidget(r, 1, cellChip(as[r].l, as[r].o));
        setCell(table, r, 2, as[r].m);
    }
    table->setWordWrap(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->setColumnWidth(0, 92);
    table->setColumnWidth(1, 112);
    al->addWidget(table, 1);
    bot->addWidget(alP, 5);

    outer->addLayout(bot, 6);
    return root;
}
