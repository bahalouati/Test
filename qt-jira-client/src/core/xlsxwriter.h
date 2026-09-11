#pragma once

#include <QHash>
#include <QList>
#include <QString>

// A very small .xlsx writer.
//
// An .xlsx is a ZIP of XML parts, so this builds the handful of parts Excel
// insists on and stores them uncompressed. That is enough for the two sheets
// the timesheet export needs -- styled cells, column widths, row heights and
// external hyperlinks -- without pulling in a spreadsheet library.
namespace xlsx {

// The only cell looks this file ever needs. Keeping it an enum rather than a
// general style model keeps styles.xml hand-writable and predictable.
enum class Style {
    Default,
    Header,          // bold white on dark blue
    Link,            // blue, underlined
    WeekdayHeader,   // bold
    DayComplete,     // green fill, wrapped, top-aligned
    DayPartial,      // amber fill
    DayShort,        // red fill
    DayFuture,       // grey fill
    DayHoliday,      // blue fill
    DayPlain         // no fill, wrapped, top-aligned
};

class Sheet
{
public:
    explicit Sheet(const QString &name);

    QString name() const { return m_name; }

    // Rows and columns are 1-based, as in Excel itself.
    void setText(int row, int column, const QString &text, Style style = Style::Default);
    void setNumber(int row, int column, double value, Style style = Style::Default);
    void setHyperlink(int row, int column, const QString &url);
    void setColumnWidth(int column, double characters);
    void setRowHeight(int row, double points);

    // "A1", "AA7" -- exposed for tests.
    static QString cellReference(int row, int column);
    static QString columnName(int column);

private:
    friend class Workbook;

    struct Cell {
        QString text;
        double number = 0.0;
        bool isNumber = false;
        Style style = Style::Default;
        QString hyperlink;
    };

    QString toXml(QString *relationships) const;

    QString m_name;
    QHash<int, QHash<int, Cell>> m_cells;   // row -> column -> cell
    QHash<int, double> m_columnWidths;
    QHash<int, double> m_rowHeights;
    int m_maxRow = 0;
    int m_maxColumn = 0;
};

class Workbook
{
public:
    ~Workbook();

    // The workbook owns the sheet.
    Sheet *addSheet(const QString &name);

    bool save(const QString &path, QString *errorMessage = nullptr) const;

private:
    QList<Sheet *> m_sheets;
};

} // namespace xlsx
