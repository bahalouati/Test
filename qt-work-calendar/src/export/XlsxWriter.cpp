#include "export/XlsxWriter.h"

#include <QDateTime>
#include <QFile>
#include <QSet>
#include <QStringList>

// =====================================================================
// A minimal zip archive
// =====================================================================
namespace {

/*! Appends a little-endian 16-bit value, as every zip field is stored. */
void appendUInt16(QByteArray &target, quint16 value)
{
    target.append(static_cast<char>(value & 0xFF));
    target.append(static_cast<char>((value >> 8) & 0xFF));
}

void appendUInt32(QByteArray &target, quint32 value)
{
    target.append(static_cast<char>(value & 0xFF));
    target.append(static_cast<char>((value >> 8) & 0xFF));
    target.append(static_cast<char>((value >> 16) & 0xFF));
    target.append(static_cast<char>((value >> 24) & 0xFF));
}

/*! The CRC-32 the zip format requires, computed with the standard table. */
quint32 crc32Of(const QByteArray &data)
{
    static quint32 table[256];
    static bool tableReady = false;

    if (!tableReady) {
        for (quint32 index = 0; index < 256; ++index) {
            quint32 value = index;
            for (int bit = 0; bit < 8; ++bit)
                value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
            table[index] = value;
        }
        tableReady = true;
    }

    quint32 crc = 0xFFFFFFFFu;
    for (int i = 0; i < data.size(); ++i) {
        const quint8 byte = static_cast<quint8>(data.at(i));
        crc = table[(crc ^ byte) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

/*!
 * \brief Collects parts and produces a zip archive.
 *
 * Entries are stored, not deflated: the format allows it, every reader accepts
 * it, and it keeps this class to fifty readable lines instead of a compression
 * dependency.
 */
class ZipArchive
{
public:
    void addFile(const QString &path, const QByteArray &content);
    QByteArray toByteArray() const;

private:
    struct Entry {
        QByteArray path;
        QByteArray content;
        quint32 crc = 0;
    };

    QVector<Entry> m_entries;
};

void ZipArchive::addFile(const QString &path, const QByteArray &content)
{
    Entry entry;
    entry.path = path.toUtf8();
    entry.content = content;
    entry.crc = crc32Of(content);
    m_entries.append(entry);
}

QByteArray ZipArchive::toByteArray() const
{
    // A fixed timestamp (1 Jan 1980, the earliest the format can express) keeps
    // exporting the same data twice byte-for-byte identical, which makes the
    // output diffable and the tests stable.
    const quint16 dosTime = 0;
    const quint16 dosDate = 0x0021;

    QByteArray archive;
    QVector<quint32> offsets;
    offsets.reserve(m_entries.size());

    for (const Entry &entry : m_entries) {
        offsets.append(static_cast<quint32>(archive.size()));

        appendUInt32(archive, 0x04034B50u); // local file header
        appendUInt16(archive, 20);          // version needed
        appendUInt16(archive, 0);           // flags
        appendUInt16(archive, 0);           // method 0 = stored
        appendUInt16(archive, dosTime);
        appendUInt16(archive, dosDate);
        appendUInt32(archive, entry.crc);
        appendUInt32(archive, static_cast<quint32>(entry.content.size()));
        appendUInt32(archive, static_cast<quint32>(entry.content.size()));
        appendUInt16(archive, static_cast<quint16>(entry.path.size()));
        appendUInt16(archive, 0);           // extra field length
        archive.append(entry.path);
        archive.append(entry.content);
    }

    const quint32 centralDirectoryOffset = static_cast<quint32>(archive.size());

    for (int index = 0; index < m_entries.size(); ++index) {
        const Entry &entry = m_entries.at(index);

        appendUInt32(archive, 0x02014B50u); // central directory header
        appendUInt16(archive, 20);          // version made by
        appendUInt16(archive, 20);          // version needed
        appendUInt16(archive, 0);           // flags
        appendUInt16(archive, 0);           // method
        appendUInt16(archive, dosTime);
        appendUInt16(archive, dosDate);
        appendUInt32(archive, entry.crc);
        appendUInt32(archive, static_cast<quint32>(entry.content.size()));
        appendUInt32(archive, static_cast<quint32>(entry.content.size()));
        appendUInt16(archive, static_cast<quint16>(entry.path.size()));
        appendUInt16(archive, 0);           // extra
        appendUInt16(archive, 0);           // comment
        appendUInt16(archive, 0);           // disk number
        appendUInt16(archive, 0);           // internal attributes
        appendUInt32(archive, 0);           // external attributes
        appendUInt32(archive, offsets.at(index));
        archive.append(entry.path);
    }

    const quint32 centralDirectorySize =
        static_cast<quint32>(archive.size()) - centralDirectoryOffset;

    appendUInt32(archive, 0x06054B50u); // end of central directory
    appendUInt16(archive, 0);           // this disk
    appendUInt16(archive, 0);           // disk with the directory
    appendUInt16(archive, static_cast<quint16>(m_entries.size()));
    appendUInt16(archive, static_cast<quint16>(m_entries.size()));
    appendUInt32(archive, centralDirectorySize);
    appendUInt32(archive, centralDirectoryOffset);
    appendUInt16(archive, 0);           // comment length

    return archive;
}

// =====================================================================
// XML helpers
// =====================================================================

/*! Escapes the five characters XML reserves, and drops control characters. */
QString escapeXml(const QString &text)
{
    QString escaped;
    escaped.reserve(text.size() + 16);
    for (int i = 0; i < text.size(); ++i) {
        const QChar character = text.at(i);
        const ushort code = character.unicode();

        // Tab, newline and carriage return are legal in XML 1.0; the rest of
        // the control range is not and would make the file unreadable.
        if (code < 0x20 && code != 0x09 && code != 0x0A && code != 0x0D)
            continue;

        switch (code) {
        case '&':  escaped += QStringLiteral("&amp;"); break;
        case '<':  escaped += QStringLiteral("&lt;"); break;
        case '>':  escaped += QStringLiteral("&gt;"); break;
        case '"':  escaped += QStringLiteral("&quot;"); break;
        case '\'': escaped += QStringLiteral("&apos;"); break;
        default:   escaped += character; break;
        }
    }
    return escaped;
}

const char *kXmlDeclaration = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";

/*! Excel refuses sheet names with these characters, or longer than 31 chars. */
QString sanitizeSheetName(const QString &name)
{
    QString cleaned = name;
    const QString forbidden = QStringLiteral("[]:*?/\\");
    for (int i = 0; i < cleaned.size(); ++i) {
        if (forbidden.contains(cleaned.at(i)))
            cleaned[i] = QLatin1Char(' ');
    }
    cleaned = cleaned.trimmed();
    if (cleaned.isEmpty())
        cleaned = QStringLiteral("Sheet");
    return cleaned.left(31);
}

} // namespace

// =====================================================================
// XlsxFormat
// =====================================================================

bool XlsxFormat::operator==(const XlsxFormat &other) const
{
    return bold == other.bold
           && italic == other.italic
           && fontSize == other.fontSize
           && fontColor == other.fontColor
           && fillColor == other.fillColor
           && wrapText == other.wrapText
           && border == other.border
           && horizontalAlignment == other.horizontalAlignment
           && verticalAlignment == other.verticalAlignment
           && numberFormat == other.numberFormat;
}

// =====================================================================
// XlsxSheet
// =====================================================================

XlsxSheet::XlsxSheet(const QString &name)
    : m_name(sanitizeSheetName(name))
{
}

QString XlsxSheet::name() const
{
    return m_name;
}

QString XlsxSheet::columnName(int column)
{
    QString name;
    int remaining = column;
    while (remaining > 0) {
        const int rest = (remaining - 1) % 26;
        name.prepend(QChar(QLatin1Char('A').toLatin1() + rest));
        remaining = (remaining - 1) / 26;
    }
    return name.isEmpty() ? QStringLiteral("A") : name;
}

QString XlsxSheet::cellReference(int row, int column)
{
    return columnName(column) + QString::number(row);
}

QString XlsxSheet::Range::reference() const
{
    return XlsxSheet::cellReference(firstRow, firstColumn)
           + QLatin1Char(':')
           + XlsxSheet::cellReference(lastRow, lastColumn);
}

void XlsxSheet::writeString(int row, int column, const QString &text, int format)
{
    if (row < 1 || column < 1)
        return;
    Cell cell;
    cell.text = text;
    cell.format = format;
    m_cells[row][column] = cell;
}

void XlsxSheet::writeNumber(int row, int column, double value, int format)
{
    if (row < 1 || column < 1)
        return;
    Cell cell;
    cell.number = value;
    cell.isNumber = true;
    cell.format = format;
    m_cells[row][column] = cell;
}

void XlsxSheet::writeHyperlink(int row, int column, const QString &text, const QString &url,
                               int format)
{
    if (row < 1 || column < 1)
        return;
    Cell cell;
    cell.text = text;
    cell.hyperlink = url;
    cell.format = format;
    m_cells[row][column] = cell;
}

void XlsxSheet::setColumnWidth(int column, double width)
{
    if (column >= 1 && width > 0.0)
        m_columnWidths.insert(column, width);
}

void XlsxSheet::setRowHeight(int row, double height)
{
    if (row >= 1 && height > 0.0)
        m_rowHeights.insert(row, height);
}

void XlsxSheet::freezePanes(int row, int column)
{
    m_freezeRow = qMax(0, row);
    m_freezeColumn = qMax(0, column);
}

void XlsxSheet::setAutoFilter(int firstRow, int firstColumn, int lastRow, int lastColumn)
{
    m_autoFilter.firstRow = firstRow;
    m_autoFilter.firstColumn = firstColumn;
    m_autoFilter.lastRow = lastRow;
    m_autoFilter.lastColumn = lastColumn;
}

void XlsxSheet::mergeCells(int firstRow, int firstColumn, int lastRow, int lastColumn)
{
    Range range;
    range.firstRow = firstRow;
    range.firstColumn = firstColumn;
    range.lastRow = lastRow;
    range.lastColumn = lastColumn;
    if (range.isValid())
        m_mergedRanges.append(range);
}

bool XlsxSheet::hasHyperlinks() const
{
    QMap<int, QMap<int, Cell>>::const_iterator rowIt = m_cells.constBegin();
    for (; rowIt != m_cells.constEnd(); ++rowIt) {
        QMap<int, Cell>::const_iterator cellIt = rowIt.value().constBegin();
        for (; cellIt != rowIt.value().constEnd(); ++cellIt) {
            if (!cellIt.value().hyperlink.isEmpty())
                return true;
        }
    }
    return false;
}

QByteArray XlsxSheet::toXml() const
{
    QString xml = QString::fromLatin1(kXmlDeclaration);
    xml += QStringLiteral(
        "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">");

    // The child elements below must appear in exactly this order; Excel rejects
    // a worksheet whose parts are shuffled.

    if (m_freezeRow > 0 || m_freezeColumn > 0) {
        const QString topLeft = cellReference(m_freezeRow + 1, m_freezeColumn + 1);
        xml += QStringLiteral("<sheetViews><sheetView workbookViewId=\"0\"><pane");
        if (m_freezeColumn > 0)
            xml += QStringLiteral(" xSplit=\"%1\"").arg(m_freezeColumn);
        if (m_freezeRow > 0)
            xml += QStringLiteral(" ySplit=\"%1\"").arg(m_freezeRow);
        xml += QStringLiteral(" topLeftCell=\"%1\" activePane=\"bottomRight\" state=\"frozen\"/>")
                   .arg(topLeft);
        xml += QStringLiteral("</sheetView></sheetViews>");
    }

    if (!m_columnWidths.isEmpty()) {
        xml += QStringLiteral("<cols>");
        QMap<int, double>::const_iterator it = m_columnWidths.constBegin();
        for (; it != m_columnWidths.constEnd(); ++it) {
            xml += QStringLiteral("<col min=\"%1\" max=\"%1\" width=\"%2\" customWidth=\"1\"/>")
                       .arg(it.key())
                       .arg(it.value(), 0, 'f', 2);
        }
        xml += QStringLiteral("</cols>");
    }

    xml += QStringLiteral("<sheetData>");
    QMap<int, QMap<int, Cell>>::const_iterator rowIt = m_cells.constBegin();
    for (; rowIt != m_cells.constEnd(); ++rowIt) {
        const int row = rowIt.key();
        xml += QStringLiteral("<row r=\"%1\"").arg(row);
        if (m_rowHeights.contains(row)) {
            xml += QStringLiteral(" ht=\"%1\" customHeight=\"1\"")
                       .arg(m_rowHeights.value(row), 0, 'f', 2);
        }
        xml += QStringLiteral(">");

        QMap<int, Cell>::const_iterator cellIt = rowIt.value().constBegin();
        for (; cellIt != rowIt.value().constEnd(); ++cellIt) {
            const Cell &cell = cellIt.value();
            const QString reference = cellReference(row, cellIt.key());

            xml += QStringLiteral("<c r=\"%1\"").arg(reference);
            if (cell.format >= 0)
                xml += QStringLiteral(" s=\"%1\"").arg(cell.format);

            if (cell.isNumber) {
                xml += QStringLiteral("><v>%1</v></c>").arg(cell.number, 0, 'f', 6);
            } else if (cell.text.isEmpty()) {
                xml += QStringLiteral("/>");
            } else {
                // Inline strings keep every cell self-contained, which costs a
                // few bytes and saves a whole shared-string table.
                xml += QStringLiteral(" t=\"inlineStr\"><is><t xml:space=\"preserve\">%1</t></is></c>")
                           .arg(escapeXml(cell.text));
            }
        }
        xml += QStringLiteral("</row>");
    }
    xml += QStringLiteral("</sheetData>");

    if (m_autoFilter.isValid())
        xml += QStringLiteral("<autoFilter ref=\"%1\"/>").arg(m_autoFilter.reference());

    if (!m_mergedRanges.isEmpty()) {
        xml += QStringLiteral("<mergeCells count=\"%1\">").arg(m_mergedRanges.size());
        for (const Range &range : m_mergedRanges)
            xml += QStringLiteral("<mergeCell ref=\"%1\"/>").arg(range.reference());
        xml += QStringLiteral("</mergeCells>");
    }

    if (hasHyperlinks()) {
        xml += QStringLiteral("<hyperlinks>");
        int relationshipId = 0;
        QMap<int, QMap<int, Cell>>::const_iterator linkRowIt = m_cells.constBegin();
        for (; linkRowIt != m_cells.constEnd(); ++linkRowIt) {
            QMap<int, Cell>::const_iterator linkCellIt = linkRowIt.value().constBegin();
            for (; linkCellIt != linkRowIt.value().constEnd(); ++linkCellIt) {
                if (linkCellIt.value().hyperlink.isEmpty())
                    continue;
                ++relationshipId;
                xml += QStringLiteral("<hyperlink ref=\"%1\" r:id=\"rId%2\"/>")
                           .arg(cellReference(linkRowIt.key(), linkCellIt.key()))
                           .arg(relationshipId);
            }
        }
        xml += QStringLiteral("</hyperlinks>");
    }

    xml += QStringLiteral("</worksheet>");
    return xml.toUtf8();
}

QByteArray XlsxSheet::relationshipsXml() const
{
    if (!hasHyperlinks())
        return QByteArray();

    QString xml = QString::fromLatin1(kXmlDeclaration);
    xml += QStringLiteral(
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");

    int relationshipId = 0;
    QMap<int, QMap<int, Cell>>::const_iterator rowIt = m_cells.constBegin();
    for (; rowIt != m_cells.constEnd(); ++rowIt) {
        QMap<int, Cell>::const_iterator cellIt = rowIt.value().constBegin();
        for (; cellIt != rowIt.value().constEnd(); ++cellIt) {
            if (cellIt.value().hyperlink.isEmpty())
                continue;
            ++relationshipId;
            xml += QStringLiteral(
                       "<Relationship Id=\"rId%1\" "
                       "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
                       "relationships/hyperlink\" Target=\"%2\" TargetMode=\"External\"/>")
                       .arg(relationshipId)
                       .arg(escapeXml(cellIt.value().hyperlink));
        }
    }

    xml += QStringLiteral("</Relationships>");
    return xml.toUtf8();
}

// =====================================================================
// XlsxWriter
// =====================================================================

XlsxWriter::XlsxWriter()
{
}

XlsxWriter::~XlsxWriter()
{
    qDeleteAll(m_sheets);
    m_sheets.clear();
}

int XlsxWriter::addFormat(const XlsxFormat &format)
{
    for (int index = 0; index < m_formats.size(); ++index) {
        if (m_formats.at(index) == format)
            return index + 1; // style 0 is the built-in default
    }
    m_formats.append(format);
    return m_formats.size();
}

XlsxSheet *XlsxWriter::addSheet(const QString &name)
{
    XlsxSheet *sheet = new XlsxSheet(name);
    m_sheets.append(sheet);
    return sheet;
}

QByteArray XlsxWriter::contentTypesXml() const
{
    QString xml = QString::fromLatin1(kXmlDeclaration);
    xml += QStringLiteral(
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" "
        "ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument."
        "spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/styles.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument."
        "spreadsheetml.styles+xml\"/>");

    for (int index = 0; index < m_sheets.size(); ++index) {
        xml += QStringLiteral(
                   "<Override PartName=\"/xl/worksheets/sheet%1.xml\" "
                   "ContentType=\"application/vnd.openxmlformats-officedocument."
                   "spreadsheetml.worksheet+xml\"/>")
                   .arg(index + 1);
    }

    xml += QStringLiteral("</Types>");
    return xml.toUtf8();
}

QByteArray XlsxWriter::rootRelationshipsXml() const
{
    QString xml = QString::fromLatin1(kXmlDeclaration);
    xml += QStringLiteral(
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
        "officeDocument\" Target=\"xl/workbook.xml\"/>"
        "</Relationships>");
    return xml.toUtf8();
}

QByteArray XlsxWriter::workbookXml() const
{
    QString xml = QString::fromLatin1(kXmlDeclaration);
    xml += QStringLiteral(
        "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<sheets>");

    for (int index = 0; index < m_sheets.size(); ++index) {
        xml += QStringLiteral("<sheet name=\"%1\" sheetId=\"%2\" r:id=\"rId%2\"/>")
                   .arg(escapeXml(m_sheets.at(index)->name()))
                   .arg(index + 1);
    }

    xml += QStringLiteral("</sheets></workbook>");
    return xml.toUtf8();
}

QByteArray XlsxWriter::workbookRelationshipsXml() const
{
    QString xml = QString::fromLatin1(kXmlDeclaration);
    xml += QStringLiteral(
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");

    for (int index = 0; index < m_sheets.size(); ++index) {
        xml += QStringLiteral(
                   "<Relationship Id=\"rId%1\" "
                   "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
                   "worksheet\" Target=\"worksheets/sheet%1.xml\"/>")
                   .arg(index + 1);
    }

    // The styles part is referenced after the sheets, so its id follows theirs.
    xml += QStringLiteral(
               "<Relationship Id=\"rId%1\" "
               "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
               "styles\" Target=\"styles.xml\"/>")
               .arg(m_sheets.size() + 1);

    xml += QStringLiteral("</Relationships>");
    return xml.toUtf8();
}

QByteArray XlsxWriter::stylesXml() const
{
    // Excel addresses fonts, fills, borders and number formats by index, so the
    // unique ones are collected first and every registered format then points
    // at the rows it needs.
    QStringList fonts;
    QStringList fills;
    QStringList borders;
    QStringList numberFormats;

    QVector<int> fontOfFormat(m_formats.size(), 0);
    QVector<int> fillOfFormat(m_formats.size(), 0);
    QVector<int> borderOfFormat(m_formats.size(), 0);
    QVector<int> numberFormatOfFormat(m_formats.size(), 0);

    fonts << QStringLiteral("<font><sz val=\"11\"/><name val=\"Calibri\"/></font>");

    // Index 0 and 1 are reserved by the format itself and must be exactly this.
    fills << QStringLiteral("<fill><patternFill patternType=\"none\"/></fill>");
    fills << QStringLiteral("<fill><patternFill patternType=\"gray125\"/></fill>");

    borders << QStringLiteral("<border><left/><right/><top/><bottom/><diagonal/></border>");
    borders << QStringLiteral(
        "<border><left style=\"thin\"><color rgb=\"FFBFBFBF\"/></left>"
        "<right style=\"thin\"><color rgb=\"FFBFBFBF\"/></right>"
        "<top style=\"thin\"><color rgb=\"FFBFBFBF\"/></top>"
        "<bottom style=\"thin\"><color rgb=\"FFBFBFBF\"/></bottom><diagonal/></border>");

    for (int index = 0; index < m_formats.size(); ++index) {
        const XlsxFormat &format = m_formats.at(index);

        if (format.bold || format.italic || format.fontSize > 0 || !format.fontColor.isEmpty()) {
            QString font = QStringLiteral("<font>");
            if (format.bold)
                font += QStringLiteral("<b/>");
            if (format.italic)
                font += QStringLiteral("<i/>");
            font += QStringLiteral("<sz val=\"%1\"/>").arg(format.fontSize > 0 ? format.fontSize : 11);
            if (!format.fontColor.isEmpty())
                font += QStringLiteral("<color rgb=\"FF%1\"/>").arg(format.fontColor);
            font += QStringLiteral("<name val=\"Calibri\"/></font>");

            const int existing = fonts.indexOf(font);
            if (existing >= 0) {
                fontOfFormat[index] = existing;
            } else {
                fonts << font;
                fontOfFormat[index] = fonts.size() - 1;
            }
        }

        if (!format.fillColor.isEmpty()) {
            const QString fill =
                QStringLiteral("<fill><patternFill patternType=\"solid\">"
                               "<fgColor rgb=\"FF%1\"/><bgColor indexed=\"64\"/>"
                               "</patternFill></fill>")
                    .arg(format.fillColor);

            const int existing = fills.indexOf(fill);
            if (existing >= 0) {
                fillOfFormat[index] = existing;
            } else {
                fills << fill;
                fillOfFormat[index] = fills.size() - 1;
            }
        }

        if (format.border)
            borderOfFormat[index] = 1;

        if (!format.numberFormat.isEmpty()) {
            const int existing = numberFormats.indexOf(format.numberFormat);
            if (existing >= 0) {
                numberFormatOfFormat[index] = 164 + existing;
            } else {
                numberFormats << format.numberFormat;
                numberFormatOfFormat[index] = 164 + numberFormats.size() - 1;
            }
        }
    }

    QString xml = QString::fromLatin1(kXmlDeclaration);
    xml += QStringLiteral(
        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">");

    if (!numberFormats.isEmpty()) {
        xml += QStringLiteral("<numFmts count=\"%1\">").arg(numberFormats.size());
        for (int index = 0; index < numberFormats.size(); ++index) {
            xml += QStringLiteral("<numFmt numFmtId=\"%1\" formatCode=\"%2\"/>")
                       .arg(164 + index)
                       .arg(escapeXml(numberFormats.at(index)));
        }
        xml += QStringLiteral("</numFmts>");
    }

    xml += QStringLiteral("<fonts count=\"%1\">%2</fonts>")
               .arg(fonts.size())
               .arg(fonts.join(QString()));
    xml += QStringLiteral("<fills count=\"%1\">%2</fills>")
               .arg(fills.size())
               .arg(fills.join(QString()));
    xml += QStringLiteral("<borders count=\"%1\">%2</borders>")
               .arg(borders.size())
               .arg(borders.join(QString()));

    xml += QStringLiteral(
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/>"
        "</cellStyleXfs>");

    xml += QStringLiteral("<cellXfs count=\"%1\">").arg(m_formats.size() + 1);
    xml += QStringLiteral("<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>");

    for (int index = 0; index < m_formats.size(); ++index) {
        const XlsxFormat &format = m_formats.at(index);
        const bool hasAlignment = format.wrapText
                                  || !format.horizontalAlignment.isEmpty()
                                  || !format.verticalAlignment.isEmpty();

        xml += QStringLiteral("<xf numFmtId=\"%1\" fontId=\"%2\" fillId=\"%3\" borderId=\"%4\" "
                              "xfId=\"0\" applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\"")
                   .arg(numberFormatOfFormat.at(index))
                   .arg(fontOfFormat.at(index))
                   .arg(fillOfFormat.at(index))
                   .arg(borderOfFormat.at(index));

        if (numberFormatOfFormat.at(index) != 0)
            xml += QStringLiteral(" applyNumberFormat=\"1\"");

        if (hasAlignment) {
            xml += QStringLiteral(" applyAlignment=\"1\"><alignment");
            if (!format.horizontalAlignment.isEmpty())
                xml += QStringLiteral(" horizontal=\"%1\"").arg(format.horizontalAlignment);
            if (!format.verticalAlignment.isEmpty())
                xml += QStringLiteral(" vertical=\"%1\"").arg(format.verticalAlignment);
            if (format.wrapText)
                xml += QStringLiteral(" wrapText=\"1\"");
            xml += QStringLiteral("/></xf>");
        } else {
            xml += QStringLiteral("/>");
        }
    }

    xml += QStringLiteral("</cellXfs>");
    xml += QStringLiteral(
        "<cellStyles count=\"1\">"
        "<cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>");
    xml += QStringLiteral("</styleSheet>");

    return xml.toUtf8();
}

QByteArray XlsxWriter::toByteArray() const
{
    ZipArchive archive;

    archive.addFile(QStringLiteral("[Content_Types].xml"), contentTypesXml());
    archive.addFile(QStringLiteral("_rels/.rels"), rootRelationshipsXml());
    archive.addFile(QStringLiteral("xl/workbook.xml"), workbookXml());
    archive.addFile(QStringLiteral("xl/_rels/workbook.xml.rels"), workbookRelationshipsXml());
    archive.addFile(QStringLiteral("xl/styles.xml"), stylesXml());

    for (int index = 0; index < m_sheets.size(); ++index) {
        const XlsxSheet *sheet = m_sheets.at(index);
        archive.addFile(QStringLiteral("xl/worksheets/sheet%1.xml").arg(index + 1), sheet->toXml());

        const QByteArray relationships = sheet->relationshipsXml();
        if (!relationships.isEmpty()) {
            archive.addFile(QStringLiteral("xl/worksheets/_rels/sheet%1.xml.rels").arg(index + 1),
                            relationships);
        }
    }

    return archive.toByteArray();
}

bool XlsxWriter::save(const QString &path, QString *error)
{
    if (m_sheets.isEmpty()) {
        if (error)
            *error = QStringLiteral("The workbook has no sheets.");
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }

    const QByteArray content = toByteArray();
    if (file.write(content) != content.size()) {
        if (error)
            *error = QStringLiteral("Writing %1 failed: %2").arg(path, file.errorString());
        return false;
    }

    file.close();
    return true;
}
