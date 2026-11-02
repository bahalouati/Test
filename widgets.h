#pragma once

// Custom-painted widgets used by the showcase designs. Pure QtWidgets + QPainter, no extra modules.

#include <QAbstractButton>
#include <QColor>
#include <QPropertyAnimation>
#include <QStringList>
#include <QVector>
#include <QWidget>

// ---------------------------------------------------------------- animated iOS-style switch
class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT
    Q_PROPERTY(qreal knob READ knob WRITE setKnob)
public:
    explicit ToggleSwitch(QWidget *parent = nullptr);
    QSize sizeHint() const override { return {46, 26}; }
    void setColors(const QColor &on, const QColor &off, const QColor &knob);
    void setCheckedInstant(bool on);   // no animation (use for initial state)
    qreal knob() const { return m_knob; }
    void setKnob(qreal v) { m_knob = v; update(); }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    qreal m_knob = 0;
    QColor m_on{"#4f6df5"}, m_off{"#3a3f55"}, m_knobColor{"#ffffff"};
    QPropertyAnimation m_anim;
};

// ---------------------------------------------------------------- 270 degree gauge
class ArcGauge : public QWidget
{
    Q_OBJECT
public:
    explicit ArcGauge(QWidget *parent = nullptr);
    void setup(const QString &title, const QString &unit, qreal value, qreal max,
               const QColor &color, const QColor &track, const QColor &text, const QColor &muted);
    QSize sizeHint() const override { return {220, 200}; }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QString m_title, m_unit;
    qreal m_value = 0, m_max = 100;
    QColor m_color, m_track, m_text, m_muted;
};

// ---------------------------------------------------------------- donut chart with centre label
class DonutChart : public QWidget
{
    Q_OBJECT
public:
    struct Slice { qreal value; QColor color; };
    explicit DonutChart(QWidget *parent = nullptr);
    void setSlices(const QVector<Slice> &s) { m_slices = s; update(); }
    void setCenter(const QString &big, const QString &small, const QColor &text, const QColor &muted);
    QSize sizeHint() const override { return {200, 200}; }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QVector<Slice> m_slices;
    QString m_big, m_small;
    QColor m_text{"#fff"}, m_muted{"#999"};
};

// ---------------------------------------------------------------- smooth area/line chart
class LineChart : public QWidget
{
    Q_OBJECT
public:
    explicit LineChart(QWidget *parent = nullptr);
    void addSeries(const QVector<qreal> &v, const QColor &c, bool area = true);
    void setLabels(const QStringList &l) { m_labels = l; update(); }
    void setTheme(const QColor &grid, const QColor &text, const QColor &bubbleBg, const QColor &bubbleText);
    void setValueFormat(const QString &prefix, const QString &suffix) { m_prefix = prefix; m_suffix = suffix; }
    void setYRange(qreal lo, qreal hi) { m_lo = lo; m_hi = hi; m_fixed = true; update(); }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    struct Series { QVector<qreal> v; QColor c; bool area; };
    QVector<Series> m_series;
    QStringList m_labels;
    QColor m_grid{255, 255, 255, 25}, m_text{"#8b93a7"}, m_bubbleBg{"#fff"}, m_bubbleText{"#111"};
    QString m_prefix, m_suffix;
    qreal m_lo = 0, m_hi = 1;
    bool m_fixed = false;
};

// ---------------------------------------------------------------- rounded bar chart
class BarChart : public QWidget
{
    Q_OBJECT
public:
    explicit BarChart(QWidget *parent = nullptr);
    void setData(const QVector<qreal> &v, const QStringList &labels, int highlight);
    void setTheme(const QColor &accent, const QColor &accent2, const QColor &bar, const QColor &track,
                  const QColor &text, const QColor &bubbleBg, const QColor &bubbleText);
    void setValueFormat(const QString &prefix, const QString &suffix) { m_prefix = prefix; m_suffix = suffix; }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QVector<qreal> m_v;
    QStringList m_labels;
    int m_hi = -1;
    QColor m_accent{"#4f6df5"}, m_accent2{"#8aa0ff"}, m_bar{"#dfe3f1"}, m_track{"#f1f3fa"};
    QColor m_text{"#8a90a8"}, m_bubbleBg{"#111"}, m_bubbleText{"#fff"};
    QString m_prefix, m_suffix;
};

// ---------------------------------------------------------------- tiny trend line
class Sparkline : public QWidget
{
    Q_OBJECT
public:
    explicit Sparkline(QWidget *parent = nullptr);
    void setData(const QVector<qreal> &v, const QColor &c) { m_v = v; m_c = c; update(); }
    QSize sizeHint() const override { return {120, 36}; }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QVector<qreal> m_v;
    QColor m_c{"#4f6df5"};
};

// ---------------------------------------------------------------- payment card
class CreditCard : public QWidget
{
    Q_OBJECT
public:
    explicit CreditCard(QWidget *parent = nullptr);
    void setup(const QString &number, const QString &holder, const QString &expiry,
               const QColor &c1, const QColor &c2, const QString &brand);
    QSize sizeHint() const override { return {360, 218}; }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QString m_number, m_holder, m_expiry, m_brand;
    QColor m_c1{"#1f2a6b"}, m_c2{"#5b6cff"};
};

// ---------------------------------------------------------------- status LED
class StatusDot : public QWidget
{
    Q_OBJECT
public:
    explicit StatusDot(const QColor &c, QWidget *parent = nullptr) : QWidget(parent), m_c(c) { setFixedSize(14, 14); }
    void setColor(const QColor &c) { m_c = c; update(); }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QColor m_c;
};
