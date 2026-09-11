#ifndef WORKCALENDAR_XLSXWRITER_H
#define WORKCALENDAR_XLSXWRITER_H

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QVector>

/*!
 * \file XlsxWriter.h
 * \brief A small, dependency-free writer for .xlsx workbooks.
 *
 * An .xlsx file is a zip archive of XML parts. Everything needed for a report -
 * text, numbers, fills, fonts, wrapped text, column widths, row heights,
 * merged cells, frozen headers, autofilter and external hyperlinks - is a
 * handful of elements, so the format is produced directly here rather than
 * pulling in a spreadsheet library.
 *
 * The archive stores its parts uncompressed. That keeps the writer to one
 * readable file with no zlib handling; a month of work entries produces a file
 * of a few tens of kilobytes either way.
 */

/*! The formatting of a cell. Register one with XlsxWriter::addFormat(). */
struct XlsxFormat
{
    bool bold = false;
    bool italic = false;
    int fontSize = 0;               //!< 0 keeps the default of 11 points
    QString fontColor;              //!< "RRGGBB", empty for the default
    QString fillColor;              //!< "RRGGBB" solid fill, empty for none
    bool wrapText = false;
    bool border = false;            //!< thin box on all four sides

    /*! "left", "center" or "right"; empty leaves it to Excel. */
    QString horizontalAlignment;

    /*! "top", "center" or "bottom"; empty leaves it to Excel. */
    QString verticalAlignment;

    /*! An Excel number format such as "0.00" or "yyyy-mm-dd". */
    QString numberFormat;

    /*! Value equality, so identical formats are only written to the file once. */
    bool operator==(const XlsxFormat &other) const;
};

/*!
 * \brief One worksheet. Rows and columns are 1-based, as in Excel itself.
 *
 * Sheets are created and owned by XlsxWriter::addSheet().
 */
class XlsxSheet
{
    friend class XlsxWriter;

public:
    /*! Writes text into a cell. \a format is an index from XlsxWriter::addFormat(). */
    void writeString(int row, int column, const QString &text, int format = -1);

    /*! Writes a number, which Excel can then sum and chart. */
    void writeNumber(int row, int column, double value, int format = -1);

    /*! Writes \a text as a clickable link to \a url. */
    void writeHyperlink(int row, int column, const QString &text, const QString &url,
                        int format = -1);

    /*! Column width in characters. */
    void setColumnWidth(int column, double width);

    /*! Row height in points. */
    void setRowHeight(int row, double height);

    /*! Keeps the rows above \a row and the columns left of \a column in view. */
    void freezePanes(int row, int column);

    /*! Adds the filter arrows over the given rectangle. */
    void setAutoFilter(int firstRow, int firstColumn, int lastRow, int lastColumn);

    /*! Merges a rectangle of cells; the value of the top-left one is shown. */
    void mergeCells(int firstRow, int firstColumn, int lastRow, int lastColumn);

    QString name() const;

    /*! The Excel reference of a cell, for example (2, 3) -> "C2". */
    static QString cellReference(int row, int column);

    /*! The Excel name of a column, for example 28 -> "AB". */
    static QString columnName(int column);

private:
    explicit XlsxSheet(const QString &name);

    struct Cell {
        QString text;
        double number = 0.0;
        bool isNumber = false;
        int format = -1;
        QString hyperlink;
    };

    struct Range {
        int firstRow = 0;
        int firstColumn = 0;
        int lastRow = 0;
        int lastColumn = 0;
        bool isValid() const { return firstRow > 0 && firstColumn > 0; }
        QString reference() const;
    };

    /*! The sheet XML, ready to be put into the archive. */
    QByteArray toXml() const;

    /*! The relationships part listing the external links, or an empty array. */
    QByteArray relationshipsXml() const;

    bool hasHyperlinks() const;

    QString m_name;
    QMap<int, QMap<int, Cell>> m_cells; //!< row -> column -> cell
    QMap<int, double> m_columnWidths;
    QMap<int, double> m_rowHeights;
    QVector<Range> m_mergedRanges;
    Range m_autoFilter;
    int m_freezeRow = 0;
    int m_freezeColumn = 0;
};

/*!
 * \brief Builds a workbook and writes it to disk.
 */
class XlsxWriter
{
public:
    XlsxWriter();
    ~XlsxWriter();

    XlsxWriter(const XlsxWriter &) = delete;
    XlsxWriter &operator=(const XlsxWriter &) = delete;

    /*!
     * \brief Registers \a format and returns its index.
     *
     * Registering the same format twice returns the same index, so callers may
     * simply ask for what they need where they need it.
     */
    int addFormat(const XlsxFormat &format);

    /*! Adds a sheet named \a name. The workbook keeps ownership. */
    XlsxSheet *addSheet(const QString &name);

    /*!
     * \brief Writes the workbook to \a path.
     * \param[out] error  why it failed, when it does
     */
    bool save(const QString &path, QString *error = nullptr);

    /*! The workbook as a byte array, which is what the tests check. */
    QByteArray toByteArray() const;

private:
    QByteArray contentTypesXml() const;
    QByteArray rootRelationshipsXml() const;
    QByteArray workbookXml() const;
    QByteArray workbookRelationshipsXml() const;
    QByteArray stylesXml() const;

    QList<XlsxSheet *> m_sheets;
    QVector<XlsxFormat> m_formats;
};

#endif // WORKCALENDAR_XLSXWRITER_H
