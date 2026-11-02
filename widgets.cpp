#include "widgets.h"

#include <QConicalGradient>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QtMath>
#include <algorithm>

namespace {

QColor lerp(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

QColor withAlpha(QColor c, int a) { c.setAlpha(a); return c; }

// Smooth curve through points using horizontal-tangent cubic segments.
QPainterPath smoothPath(const QVector<QPointF> &pts)
{
    QPainterPath path;
    if (pts.isEmpty())
        return path;
    path.moveTo(pts.first());
    for (int i = 0; i + 1 < pts.size(); ++i) {
        const qreal mx = (pts[i].x() + pts[i + 1].x()) / 2.0;
        path.cubicTo(QPointF(mx, pts[i].y()), QPointF(mx, pts[i + 1].y()), pts[i + 1]);
    }
    return path;
}

void speechBubble(QPainter &p, const QPointF &anchor, const QString &text, const QColor &bg, const QColor &fg, QFont f)
{
    f.setPixelSize(12);
    f.setBold(true);
    p.setFont(f);
    QFontMetricsF fm(f);
    const qreal w = fm.horizontalAdvance(text) + 20, h = 26;
    QRectF r(anchor.x() - w / 2, anchor.y() - h - 12, w, h);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(r, 8, 8);
    QPainterPath tri;
    tri.moveTo(anchor.x() - 5, r.bottom());
    tri.lineTo(anchor.x() + 5, r.bottom());
    tri.lineTo(anchor.x(), r.bottom() + 6);
    tri.closeSubpath();
    p.drawPath(tri);
    p.setPen(fg);
    p.drawText(r, Qt::AlignCenter, text);
}

} // namespace

// ============================================================ ToggleSwitch
ToggleSwitch::ToggleSwitch(QWidget *parent) : QAbstractButton(parent), m_anim(this, "knob", this)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(sizeHint());
    m_anim.setDuration(160);
    m_anim.setEasingCurve(QEasingCurve::InOutCubic);
    connect(this, &QAbstractButton::toggled, this, [this](bool on) {
        m_anim.stop();
        m_anim.setStartValue(m_knob);
        m_anim.setEndValue(on ? 1.0 : 0.0);
        m_anim.start();
    });
}

void ToggleSwitch::setColors(const QColor &on, const QColor &off, const QColor &knob)
{
    m_on = on; m_off = off; m_knobColor = knob; update();
}

void ToggleSwitch::setCheckedInstant(bool on)
{
    blockSignals(true);
    setChecked(on);
    blockSignals(false);
    m_knob = on ? 1.0 : 0.0;
    update();
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    p.setPen(Qt::NoPen);
    p.setBrush(lerp(m_off, m_on, m_knob));
    p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
    const qreal d = r.height() - 6;
    const qreal x = r.left() + 3 + m_knob * (r.width() - d - 6);
    p.setBrush(withAlpha(Qt::black, 40));
    p.drawEllipse(QRectF(x, r.top() + 4, d, d));          // soft shadow
    p.setBrush(m_knobColor);
    p.drawEllipse(QRectF(x, r.top() + 3, d, d));
}

// ============================================================ ArcGauge
ArcGauge::ArcGauge(QWidget *parent) : QWidget(parent) {}

void ArcGauge::setup(const QString &title, const QString &unit, qreal value, qreal max,
                     const QColor &color, const QColor &track, const QColor &text, const QColor &muted)
{
    m_title = title; m_unit = unit; m_value = value; m_max = max;
    m_color = color; m_track = track; m_text = text; m_muted = muted;
    update();
}

void ArcGauge::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal side = qMin(width(), height() - 18) - 28;
    const QPointF c(width() / 2.0, (height() - 18) / 2.0 + 4);
    const QRectF r(c.x() - side / 2, c.y() - side / 2, side, side);
    const qreal pw = 13;
    const qreal frac = qBound(0.0, m_value / m_max, 1.0);

    // tick marks
    p.setPen(QPen(withAlpha(m_muted, 90), 1.4, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i <= 27; ++i) {
        const qreal a = qDegreesToRadians(225.0 - i * 270.0 / 27.0);
        const qreal r1 = side / 2 + 12, r2 = side / 2 + (i % 3 == 0 ? 19 : 16);
        p.drawLine(QPointF(c.x() + r1 * qCos(a), c.y() - r1 * qSin(a)),
                   QPointF(c.x() + r2 * qCos(a), c.y() - r2 * qSin(a)));
    }

    // track
    QPen pen(m_track, pw, Qt::SolidLine, Qt::RoundCap);
    p.setPen(pen);
    p.drawArc(r, 225 * 16, -270 * 16);

    // glow + value arc
    for (int i = 3; i >= 1; --i) {
        QPen g(withAlpha(m_color, 14 * (4 - i)), pw + i * 5, Qt::SolidLine, Qt::RoundCap);
        p.setPen(g);
        p.drawArc(r, 225 * 16, int(-270 * 16 * frac));
    }
    p.setPen(QPen(m_color, pw, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(r, 225 * 16, int(-270 * 16 * frac));

    // centre text
    QFont f = font();
    f.setPixelSize(int(side * 0.26));
    f.setWeight(QFont::Bold);
    p.setFont(f);
    p.setPen(m_text);
    p.drawText(QRectF(r.left(), r.center().y() - side * 0.22, r.width(), side * 0.36), Qt::AlignCenter,
               QString::number(m_value, 'f', m_value < 100 && qFuzzyCompare(m_value, qRound(m_value)) == false ? 1 : 0));
    f.setPixelSize(12);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(m_muted);
    p.drawText(QRectF(r.left(), r.center().y() + side * 0.10, r.width(), 18), Qt::AlignCenter, m_unit);

    // title
    f.setPixelSize(13);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(m_text);
    p.drawText(QRectF(0, height() - 20, width(), 18), Qt::AlignCenter, m_title);
}

// ============================================================ DonutChart
DonutChart::DonutChart(QWidget *parent) : QWidget(parent) {}

void DonutChart::setCenter(const QString &big, const QString &small, const QColor &text, const QColor &muted)
{
    m_big = big; m_small = small; m_text = text; m_muted = muted; update();
}

void DonutChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal side = qMin(width(), height()) - 24;
    const QRectF r((width() - side) / 2, (height() - side) / 2, side, side);
    const qreal pw = side * 0.14;
    const QRectF ar = r.adjusted(pw / 2, pw / 2, -pw / 2, -pw / 2);

    qreal total = 0;
    for (const auto &s : m_slices) total += s.value;
    qreal angle = 90.0;
    const qreal gap = 3.0;
    for (const auto &s : m_slices) {
        const qreal span = 360.0 * s.value / total;
        p.setPen(QPen(s.color, pw, Qt::SolidLine, Qt::FlatCap));
        p.drawArc(ar, int((angle - gap / 2) * 16), int(-(span - gap) * 16));
        angle -= span;
    }

    QFont f = font();
    f.setPixelSize(int(side * 0.17));
    f.setWeight(QFont::Bold);
    p.setFont(f);
    p.setPen(m_text);
    p.drawText(QRectF(r.left(), r.center().y() - side * 0.17, r.width(), side * 0.22), Qt::AlignCenter, m_big);
    f.setPixelSize(12);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(m_muted);
    p.drawText(QRectF(r.left(), r.center().y() + side * 0.04, r.width(), 18), Qt::AlignCenter, m_small);
}

// ============================================================ LineChart
LineChart::LineChart(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(180);
}

void LineChart::addSeries(const QVector<qreal> &v, const QColor &c, bool area)
{
    m_series.push_back({v, c, area});
    update();
}

void LineChart::setTheme(const QColor &grid, const QColor &text, const QColor &bubbleBg, const QColor &bubbleText)
{
    m_grid = grid; m_text = text; m_bubbleBg = bubbleBg; m_bubbleText = bubbleText; update();
}

void LineChart::paintEvent(QPaintEvent *)
{
    if (m_series.isEmpty())
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    qreal lo = m_lo, hi = m_hi;
    if (!m_fixed) {
        lo = 0; hi = 0;
        for (const auto &s : m_series)
            for (qreal v : s.v) hi = qMax(hi, v);
        hi *= 1.18;
    }

    const QRectF plot(46, 12, width() - 46 - 14, height() - 12 - 30);
    QFont f = font();
    f.setPixelSize(11);
    p.setFont(f);

    // grid + y labels
    const int rows = 4;
    for (int i = 0; i <= rows; ++i) {
        const qreal y = plot.bottom() - plot.height() * i / rows;
        QPen gp(m_grid, 1, i == 0 ? Qt::SolidLine : Qt::DashLine);
        p.setPen(gp);
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(m_text);
        const qreal v = lo + (hi - lo) * i / rows;
        p.drawText(QRectF(0, y - 9, 40, 18), Qt::AlignRight | Qt::AlignVCenter,
                   m_prefix + (v >= 1000 ? QString::number(v / 1000, 'f', 0) + "k" : QString::number(v, 'f', 0)) + m_suffix);
    }

    const int n = m_series.first().v.size();
    // x labels
    p.setPen(m_text);
    for (int i = 0; i < m_labels.size() && i < n; ++i) {
        const qreal x = plot.left() + plot.width() * i / qMax(1, n - 1);
        p.drawText(QRectF(x - 24, plot.bottom() + 8, 48, 18), Qt::AlignCenter, m_labels[i]);
    }

    QPointF lastPt;
    qreal lastVal = 0;
    QColor lastColor;
    for (const auto &s : m_series) {
        QVector<QPointF> pts;
        for (int i = 0; i < s.v.size(); ++i)
            pts.push_back({plot.left() + plot.width() * i / qMax(1, s.v.size() - 1),
                           plot.bottom() - plot.height() * (s.v[i] - lo) / (hi - lo)});
        const QPainterPath line = smoothPath(pts);

        if (s.area) {
            QPainterPath area = line;
            area.lineTo(pts.last().x(), plot.bottom());
            area.lineTo(pts.first().x(), plot.bottom());
            area.closeSubpath();
            QLinearGradient g(0, plot.top(), 0, plot.bottom());
            g.setColorAt(0, withAlpha(s.c, 110));
            g.setColorAt(1, withAlpha(s.c, 0));
            p.setPen(Qt::NoPen);
            p.setBrush(g);
            p.drawPath(area);
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(s.c, 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(line);
        lastPt = pts.last();
        lastVal = s.v.last();
        lastColor = s.c;
    }

    // highlight last point of the last series
    p.setPen(Qt::NoPen);
    p.setBrush(withAlpha(lastColor, 60));
    p.drawEllipse(lastPt, 10, 10);
    p.setBrush(m_bubbleBg);
    p.drawEllipse(lastPt, 5.5, 5.5);
    p.setBrush(lastColor);
    p.drawEllipse(lastPt, 3.5, 3.5);
    const QString txt = m_prefix + QLocale().toString(qRound(lastVal)) + m_suffix;
    speechBubble(p, QPointF(qMin(lastPt.x(), width() - 46.0), lastPt.y() - 6), txt, m_bubbleBg, m_bubbleText, font());
}

// ============================================================ BarChart
BarChart::BarChart(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(170);
}

void BarChart::setData(const QVector<qreal> &v, const QStringList &labels, int highlight)
{
    m_v = v; m_labels = labels; m_hi = highlight; update();
}

void BarChart::setTheme(const QColor &accent, const QColor &accent2, const QColor &bar, const QColor &track,
                        const QColor &text, const QColor &bubbleBg, const QColor &bubbleText)
{
    m_accent = accent; m_accent2 = accent2; m_bar = bar; m_track = track;
    m_text = text; m_bubbleBg = bubbleBg; m_bubbleText = bubbleText; update();
}

void BarChart::paintEvent(QPaintEvent *)
{
    if (m_v.isEmpty())
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal maxV = *std::max_element(m_v.begin(), m_v.end());
    const int n = m_v.size();
    const QRectF plot(6, 38, width() - 12, height() - 38 - 26);
    const qreal slot = plot.width() / n;
    const qreal bw = qMin(34.0, slot * 0.56);

    QFont f = font();
    f.setPixelSize(12);
    p.setFont(f);

    for (int i = 0; i < n; ++i) {
        const qreal cx = plot.left() + slot * (i + 0.5);
        const QRectF track(cx - bw / 2, plot.top(), bw, plot.height());
        p.setPen(Qt::NoPen);
        p.setBrush(m_track);
        p.drawRoundedRect(track, bw / 2, bw / 2);

        const qreal h = qMax(bw, plot.height() * m_v[i] / maxV);
        const QRectF bar(cx - bw / 2, plot.bottom() - h, bw, h);
        if (i == m_hi) {
            QLinearGradient g(bar.topLeft(), bar.bottomLeft());
            g.setColorAt(0, m_accent2);
            g.setColorAt(1, m_accent);
            p.setBrush(g);
        } else {
            p.setBrush(m_bar);
        }
        p.drawRoundedRect(bar, bw / 2, bw / 2);

        p.setPen(i == m_hi ? m_accent : m_text);
        QFont lf = f;
        lf.setBold(i == m_hi);
        p.setFont(lf);
        p.drawText(QRectF(cx - slot / 2, plot.bottom() + 6, slot, 18), Qt::AlignCenter,
                   i < m_labels.size() ? m_labels[i] : QString());
        p.setFont(f);

        if (i == m_hi)
            speechBubble(p, QPointF(cx, bar.top() - 2), m_prefix + QLocale().toString(qRound(m_v[i])) + m_suffix,
                         m_bubbleBg, m_bubbleText, font());
    }
}

// ============================================================ Sparkline
Sparkline::Sparkline(QWidget *parent) : QWidget(parent) {}

void Sparkline::paintEvent(QPaintEvent *)
{
    if (m_v.size() < 2)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto mm = std::minmax_element(m_v.begin(), m_v.end());
    const qreal lo = *mm.first, hi = *mm.second;
    const QRectF r = QRectF(rect()).adjusted(2, 4, -2, -4);
    QVector<QPointF> pts;
    for (int i = 0; i < m_v.size(); ++i)
        pts.push_back({r.left() + r.width() * i / (m_v.size() - 1),
                       r.bottom() - r.height() * (m_v[i] - lo) / qMax(1e-9, hi - lo)});
    const QPainterPath line = smoothPath(pts);
    QPainterPath area = line;
    area.lineTo(pts.last().x(), r.bottom() + 4);
    area.lineTo(pts.first().x(), r.bottom() + 4);
    area.closeSubpath();
    QLinearGradient g(0, 0, 0, height());
    g.setColorAt(0, withAlpha(m_c, 80));
    g.setColorAt(1, withAlpha(m_c, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawPath(area);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(m_c, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(line);
}

// ============================================================ CreditCard
CreditCard::CreditCard(QWidget *parent) : QWidget(parent) { setFixedSize(sizeHint()); }

void CreditCard::setup(const QString &number, const QString &holder, const QString &expiry,
                       const QColor &c1, const QColor &c2, const QString &brand)
{
    m_number = number; m_holder = holder; m_expiry = expiry; m_c1 = c1; m_c2 = c2; m_brand = brand;
    update();
}

void CreditCard::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(4, 4, -4, -8);

    // drop shadow (stacked translucent rounded rects)
    p.setPen(Qt::NoPen);
    for (int i = 6; i >= 1; --i) {
        p.setBrush(withAlpha(m_c1, 12));
        p.drawRoundedRect(r.translated(0, 3 + i * 1.6).adjusted(i, 0, -i, 0), 20, 20);
    }

    QPainterPath clip;
    clip.addRoundedRect(r, 20, 20);
    p.setClipPath(clip);
    QLinearGradient g(r.topLeft(), r.bottomRight());
    g.setColorAt(0, m_c1);
    g.setColorAt(1, m_c2);
    p.fillPath(clip, g);

    // decorative soft circles
    p.setPen(Qt::NoPen);
    QRadialGradient rg(r.right() - 30, r.top() + 20, 190);
    rg.setColorAt(0, QColor(255, 255, 255, 80));
    rg.setColorAt(1, QColor(255, 255, 255, 0));
    p.setBrush(rg);
    p.drawEllipse(QPointF(r.right() - 30, r.top() + 20), 190, 190);
    p.setBrush(QColor(255, 255, 255, 16));
    p.drawEllipse(QPointF(r.left() + 40, r.bottom() + 30), 120, 120);
    p.setClipping(false);

    // brand
    QFont f = font();
    f.setPixelSize(18);
    f.setWeight(QFont::Bold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 3);
    p.setFont(f);
    p.setPen(QColor(255, 255, 255, 235));
    p.drawText(QPointF(r.left() + 24, r.top() + 42), m_brand);

    // contactless
    p.setPen(QPen(QColor(255, 255, 255, 220), 2.2, Qt::SolidLine, Qt::RoundCap));
    const QPointF cc(r.right() - 38, r.top() + 34);
    for (int i = 1; i <= 3; ++i) {
        const qreal rad = 5 + i * 5;
        p.drawArc(QRectF(cc.x() - rad, cc.y() - rad, rad * 2, rad * 2), -40 * 16, 80 * 16);
    }

    // chip
    QRectF chip(r.left() + 24, r.top() + 70, 44, 34);
    QLinearGradient cg(chip.topLeft(), chip.bottomRight());
    cg.setColorAt(0, QColor("#f6e3a1"));
    cg.setColorAt(1, QColor("#c9a24b"));
    p.setPen(Qt::NoPen);
    p.setBrush(cg);
    p.drawRoundedRect(chip, 7, 7);
    p.setPen(QPen(QColor(120, 90, 30, 120), 1));
    p.drawLine(chip.left() + 2, chip.center().y(), chip.right() - 2, chip.center().y());
    p.drawLine(chip.center().x(), chip.top() + 2, chip.center().x(), chip.bottom() - 2);

    // number
    f = font();
    f.setPixelSize(22);
    f.setWeight(QFont::DemiBold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 2.5);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QPointF(r.left() + 24, r.top() + 146), m_number);

    // holder / expiry
    f = font();
    f.setPixelSize(10);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    p.setFont(f);
    p.setPen(QColor(255, 255, 255, 170));
    p.drawText(QPointF(r.left() + 24, r.bottom() - 36), "CARD HOLDER");
    p.drawText(QPointF(r.right() - 90, r.bottom() - 36), "EXPIRES");
    f.setPixelSize(14);
    f.setWeight(QFont::DemiBold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QPointF(r.left() + 24, r.bottom() - 16), m_holder);
    p.drawText(QPointF(r.right() - 90, r.bottom() - 16), m_expiry);
}

// ============================================================ StatusDot
void StatusDot::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(withAlpha(m_c, 55));
    p.drawEllipse(rect());
    p.setBrush(m_c);
    p.drawEllipse(rect().adjusted(3, 3, -3, -3));
}
