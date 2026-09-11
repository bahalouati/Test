#include "xlsxwriter.h"

#include <QDateTime>
#include <QFile>
#include <QMap>
#include <QObject>
#include <QSet>

#include <algorithm>

namespace xlsx {

namespace {

// ---------------------------------------------------------------------------
// ZIP container. Entries are stored rather than deflated: an .xlsx of a month
// of worklogs is small, and stored entries keep this to arithmetic that is easy
// to verify instead of a compression dependency.
// ---------------------------------------------------------------------------

quint32 crc32Of(const QByteArray &data)
{
    static quint32 table[256];
    static bool built = false;
    if (!built) {
        for (quint32 index = 0; index < 256; ++index) {
            quint32 value = index;
            for (int bit = 0; bit < 8; ++bit)
                value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
            table[index] = value;
        }
        built = true;
    }

    quint32 crc = 0xFFFFFFFFu;
    for (char byte : data)
        crc = table[(crc ^ quint8(byte)) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void appendLe16(QByteArray &out, quint16 value)
{
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
}

void appendLe32(QByteArray &out, quint32 value)
{
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
    out.append(char((value >> 16) & 0xFF));
    out.append(char((value >> 24) & 0xFF));
}

struct ZipEntry {
    QByteArray name;
    QByteArray data;
    quint32 crc = 0;
    quint32 offset = 0;
};

QByteArray buildZip(const QList<QPair<QString, QString>> &parts)
{
    // MS-DOS date/time: the ZIP format's own encoding, not a Unix timestamp.
    const QDateTime now = QDateTime::currentDateTime();
    const quint16 dosTime = quint16((now.time().hour() << 11) | (now.time().minute() << 5)
                                    | (now.time().second() / 2));
    const quint16 dosDate = quint16(((now.date().year() - 1980) << 9) | (now.date().month() << 5)
                                    | now.date().day());

    QByteArray out;
    QList<ZipEntry> entries;
    entries.reserve(parts.size());

    for (const auto &part : parts) {
        ZipEntry entry;
        entry.name = part.first.toUtf8();
        entry.data = part.second.toUtf8();
        entry.crc = crc32Of(entry.data);
        entry.offset = quint32(out.size());

        out.append("PK\x03\x04", 4);
        appendLe16(out, 20);                       // version needed
        appendLe16(out, 0);                        // flags
        appendLe16(out, 0);                        // method: stored
        appendLe16(out, dosTime);
        appendLe16(out, dosDate);
        appendLe32(out, entry.crc);
        appendLe32(out, quint32(entry.data.size()));   // compressed
        appendLe32(out, quint32(entry.data.size()));   // uncompressed
        appendLe16(out, quint16(entry.name.size()));
        appendLe16(out, 0);                        // extra length
        out.append(entry.name);
        out.append(entry.data);

        entries.append(entry);
    }

    const quint32 directoryOffset = quint32(out.size());
    for (const ZipEntry &entry : entries) {
        out.append("PK\x01\x02", 4);
        appendLe16(out, 20);                       // version made by
        appendLe16(out, 20);                       // version needed
        appendLe16(out, 0);
        appendLe16(out, 0);                        // stored
        appendLe16(out, dosTime);
        appendLe16(out, dosDate);
        appendLe32(out, entry.crc);
        appendLe32(out, quint32(entry.data.size()));
        appendLe32(out, quint32(entry.data.size()));
        appendLe16(out, quint16(entry.name.size()));
        appendLe16(out, 0);                        // extra
        appendLe16(out, 0);                        // comment
        appendLe16(out, 0);                        // disk number
        appendLe16(out, 0);                        // internal attributes
        appendLe32(out, 0);                        // external attributes
        appendLe32(out, entry.offset);
        out.append(entry.name);
    }
    const quint32 directorySize = quint32(out.size()) - directoryOffset;

    out.append("PK\x05\x06", 4);
    appendLe16(out, 0);
    appendLe16(out, 0);
    appendLe16(out, quint16(entries.size()));
    appendLe16(out, quint16(entries.size()));
    appendLe32(out, directorySize);
    appendLe32(out, directoryOffset);
    appendLe16(out, 0);                            // comment length
    return out;
}

// ---------------------------------------------------------------------------
// XML
// ---------------------------------------------------------------------------

QString escapeXml(const QString &text)
{
    QString escaped;
    escaped.reserve(text.size());
    for (const QChar ch : text) {
        switch (ch.unicode()) {
        case u'&':  escaped += QLatin1String("&amp;");  break;
        case u'<':  escaped += QLatin1String("&lt;");   break;
        case u'>':  escaped += QLatin1String("&gt;");   break;
        case u'"':  escaped += QLatin1String("&quot;"); break;
        case u'\'': escaped += QLatin1String("&apos;"); break;
        default:
            // Control characters other than tab/newline/return are illegal in
            // XML 1.0 and would make Excel reject the whole file.
            if (ch.unicode() < 0x20 && ch != u'\t' && ch != u'\n' && ch != u'\r')
                escaped += QLatin1Char(' ');
            else
                escaped += ch;
        }
    }
    return escaped;
}

constexpr auto kXmlHeader = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";

// Index into styles.xml's cellXfs, in the order it writes them.
int styleIndex(Style style)
{
    switch (style) {
    case Style::Default:       return 0;
    case Style::Header:        return 1;
    case Style::Link:          return 2;
    case Style::WeekdayHeader: return 3;
    case Style::DayComplete:   return 4;
    case Style::DayPartial:    return 5;
    case Style::DayShort:      return 6;
    case Style::DayFuture:     return 7;
    case Style::DayPlain:      return 8;
    }
    return 0;
}

QString stylesXml()
{
    return QLatin1String(kXmlHeader)
            + QLatin1String(
R"(<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
<fonts count="4">
<font><sz val="11"/><name val="Calibri"/></font>
<font><b/><color rgb="FFFFFFFF"/><sz val="11"/><name val="Calibri"/></font>
<font><u/><color rgb="FF0563C1"/><sz val="11"/><name val="Calibri"/></font>
<font><b/><sz val="11"/><name val="Calibri"/></font>
</fonts>
<fills count="7">
<fill><patternFill patternType="none"/></fill>
<fill><patternFill patternType="gray125"/></fill>
<fill><patternFill patternType="solid"><fgColor rgb="FF1F4E78"/><bgColor indexed="64"/></patternFill></fill>
<fill><patternFill patternType="solid"><fgColor rgb="FFC6EFCE"/><bgColor indexed="64"/></patternFill></fill>
<fill><patternFill patternType="solid"><fgColor rgb="FFFFF2CC"/><bgColor indexed="64"/></patternFill></fill>
<fill><patternFill patternType="solid"><fgColor rgb="FFFFC7CE"/><bgColor indexed="64"/></patternFill></fill>
<fill><patternFill patternType="solid"><fgColor rgb="FFD9D9D9"/><bgColor indexed="64"/></patternFill></fill>
</fills>
<borders count="1"><border><left/><right/><top/><bottom/><diagonal/></border></borders>
<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>
<cellXfs count="9">
<xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>
<xf numFmtId="0" fontId="1" fillId="2" borderId="0" xfId="0" applyFont="1" applyFill="1"/>
<xf numFmtId="0" fontId="2" fillId="0" borderId="0" xfId="0" applyFont="1"/>
<xf numFmtId="0" fontId="3" fillId="0" borderId="0" xfId="0" applyFont="1"/>
<xf numFmtId="0" fontId="0" fillId="3" borderId="0" xfId="0" applyFill="1" applyAlignment="1"><alignment vertical="top" wrapText="1"/></xf>
<xf numFmtId="0" fontId="0" fillId="4" borderId="0" xfId="0" applyFill="1" applyAlignment="1"><alignment vertical="top" wrapText="1"/></xf>
<xf numFmtId="0" fontId="0" fillId="5" borderId="0" xfId="0" applyFill="1" applyAlignment="1"><alignment vertical="top" wrapText="1"/></xf>
<xf numFmtId="0" fontId="0" fillId="6" borderId="0" xfId="0" applyFill="1" applyAlignment="1"><alignment vertical="top" wrapText="1"/></xf>
<xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0" applyAlignment="1"><alignment vertical="top" wrapText="1"/></xf>
</cellXfs>
<cellStyles count="1"><cellStyle name="Normal" xfId="0" builtinId="0"/></cellStyles>
</styleSheet>
)");
}

} // namespace

// ---------------------------------------------------------------------------
// Sheet
// ---------------------------------------------------------------------------

Sheet::Sheet(const QString &name)
    : m_name(name)
{
}

QString Sheet::columnName(int column)
{
    QString name;
    int remaining = column;
    while (remaining > 0) {
        const int digit = (remaining - 1) % 26;
        name.prepend(QChar(u'A' + digit));
        remaining = (remaining - 1) / 26;
    }
    return name;
}

QString Sheet::cellReference(int row, int column)
{
    return columnName(column) + QString::number(row);
}

void Sheet::setText(int row, int column, const QString &text, Style style)
{
    if (row < 1 || column < 1)
        return;
    Cell &cell = m_cells[row][column];
    cell.text = text;
    cell.isNumber = false;
    cell.style = style;
    m_maxRow = qMax(m_maxRow, row);
    m_maxColumn = qMax(m_maxColumn, column);
}

void Sheet::setNumber(int row, int column, double value, Style style)
{
    if (row < 1 || column < 1)
        return;
    Cell &cell = m_cells[row][column];
    cell.number = value;
    cell.isNumber = true;
    cell.style = style;
    m_maxRow = qMax(m_maxRow, row);
    m_maxColumn = qMax(m_maxColumn, column);
}

void Sheet::setHyperlink(int row, int column, const QString &url)
{
    if (row < 1 || column < 1 || url.isEmpty())
        return;
    m_cells[row][column].hyperlink = url;
}

void Sheet::setColumnWidth(int column, double characters)
{
    if (column >= 1)
        m_columnWidths.insert(column, characters);
}

void Sheet::setRowHeight(int row, double points)
{
    if (row >= 1)
        m_rowHeights.insert(row, points);
}

QString Sheet::toXml(QString *relationships) const
{
    QString xml = QLatin1String(kXmlHeader);
    xml += QLatin1String("<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                         "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">");

    if (!m_columnWidths.isEmpty()) {
        xml += QLatin1String("<cols>");
        QList<int> columns = m_columnWidths.keys();
        std::sort(columns.begin(), columns.end());
        for (int column : columns) {
            xml += QStringLiteral("<col min=\"%1\" max=\"%1\" width=\"%2\" customWidth=\"1\"/>")
                           .arg(column)
                           .arg(m_columnWidths.value(column), 0, 'f', 2);
        }
        xml += QLatin1String("</cols>");
    }

    xml += QLatin1String("<sheetData>");

    QString hyperlinkXml;
    QString relationshipXml;
    int relationshipId = 0;

    QList<int> rows = m_cells.keys();
    std::sort(rows.begin(), rows.end());
    for (int row : rows) {
        const QHash<int, Cell> &cells = m_cells.value(row);
        xml += QStringLiteral("<row r=\"%1\"").arg(row);
        if (m_rowHeights.contains(row)) {
            xml += QStringLiteral(" ht=\"%1\" customHeight=\"1\"")
                           .arg(m_rowHeights.value(row), 0, 'f', 2);
        }
        xml += QLatin1Char('>');

        QList<int> columns = cells.keys();
        std::sort(columns.begin(), columns.end());
        for (int column : columns) {
            const Cell &cell = cells.value(column);
            const QString reference = cellReference(row, column);
            const int style = styleIndex(cell.style);

            if (cell.isNumber) {
                xml += QStringLiteral("<c r=\"%1\" s=\"%2\"><v>%3</v></c>")
                               .arg(reference)
                               .arg(style)
                               .arg(cell.number, 0, 'g', 15);
            } else if (!cell.text.isEmpty()) {
                // Inline strings avoid a shared-strings part entirely.
                xml += QStringLiteral("<c r=\"%1\" s=\"%2\" t=\"inlineStr\"><is><t xml:space=\"preserve\">%3</t></is></c>")
                               .arg(reference)
                               .arg(style)
                               .arg(escapeXml(cell.text));
            } else {
                xml += QStringLiteral("<c r=\"%1\" s=\"%2\"/>").arg(reference).arg(style);
            }

            if (!cell.hyperlink.isEmpty()) {
                ++relationshipId;
                const QString id = QStringLiteral("rId%1").arg(relationshipId);
                hyperlinkXml += QStringLiteral("<hyperlink ref=\"%1\" r:id=\"%2\"/>").arg(reference, id);
                relationshipXml +=
                        QStringLiteral("<Relationship Id=\"%1\" "
                                       "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink\" "
                                       "Target=\"%2\" TargetMode=\"External\"/>")
                                .arg(id, escapeXml(cell.hyperlink));
            }
        }
        xml += QLatin1String("</row>");
    }

    xml += QLatin1String("</sheetData>");
    if (!hyperlinkXml.isEmpty())
        xml += QLatin1String("<hyperlinks>") + hyperlinkXml + QLatin1String("</hyperlinks>");
    xml += QLatin1String("</worksheet>");

    if (relationships) {
        if (relationshipXml.isEmpty()) {
            relationships->clear();
        } else {
            *relationships = QLatin1String(kXmlHeader)
                    + QLatin1String("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">")
                    + relationshipXml + QLatin1String("</Relationships>");
        }
    }
    return xml;
}

// ---------------------------------------------------------------------------
// Workbook
// ---------------------------------------------------------------------------

Workbook::~Workbook()
{
    qDeleteAll(m_sheets);
}

Sheet *Workbook::addSheet(const QString &name)
{
    auto *sheet = new Sheet(name);
    m_sheets.append(sheet);
    return sheet;
}

bool Workbook::save(const QString &path, QString *errorMessage) const
{
    if (m_sheets.isEmpty()) {
        if (errorMessage)
            *errorMessage = QObject::tr("The workbook has no sheets.");
        return false;
    }

    QList<QPair<QString, QString>> parts;

    QString contentTypes = QLatin1String(kXmlHeader)
            + QLatin1String("<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                            "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                            "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                            "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
                            "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>");

    QString workbookSheets;
    QString workbookRels;
    for (int index = 0; index < m_sheets.size(); ++index) {
        const int number = index + 1;
        contentTypes += QStringLiteral("<Override PartName=\"/xl/worksheets/sheet%1.xml\" "
                                       "ContentType=\"application/vnd.openxmlformats-officedocument."
                                       "spreadsheetml.worksheet+xml\"/>")
                                .arg(number);
        workbookSheets += QStringLiteral("<sheet name=\"%1\" sheetId=\"%2\" r:id=\"rId%2\"/>")
                                  .arg(escapeXml(m_sheets.at(index)->name()))
                                  .arg(number);
        workbookRels += QStringLiteral("<Relationship Id=\"rId%1\" "
                                       "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" "
                                       "Target=\"worksheets/sheet%1.xml\"/>")
                                .arg(number);

        QString sheetRels;
        parts.append({QStringLiteral("xl/worksheets/sheet%1.xml").arg(number),
                      m_sheets.at(index)->toXml(&sheetRels)});
        if (!sheetRels.isEmpty()) {
            parts.append({QStringLiteral("xl/worksheets/_rels/sheet%1.xml.rels").arg(number),
                          sheetRels});
        }
    }
    contentTypes += QLatin1String("</Types>");

    // Styles come after the sheets so their relationship id does not collide.
    workbookRels += QStringLiteral("<Relationship Id=\"rId%1\" "
                                   "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
                                   "Target=\"styles.xml\"/>")
                            .arg(m_sheets.size() + 1);

    parts.prepend({QStringLiteral("xl/styles.xml"), stylesXml()});
    parts.prepend({QStringLiteral("xl/_rels/workbook.xml.rels"),
                   QLatin1String(kXmlHeader)
                           + QLatin1String("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">")
                           + workbookRels + QLatin1String("</Relationships>")});
    parts.prepend({QStringLiteral("xl/workbook.xml"),
                   QLatin1String(kXmlHeader)
                           + QLatin1String("<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                                           "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets>")
                           + workbookSheets + QLatin1String("</sheets></workbook>")});
    parts.prepend({QStringLiteral("_rels/.rels"),
                   QLatin1String(kXmlHeader)
                           + QLatin1String("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                                           "<Relationship Id=\"rId1\" "
                                           "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
                                           "Target=\"xl/workbook.xml\"/></Relationships>")});
    // [Content_Types].xml has to be the first entry in the archive.
    parts.prepend({QStringLiteral("[Content_Types].xml"), contentTypes});

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }
    const QByteArray archive = buildZip(parts);
    const qint64 written = file.write(archive);
    file.close();

    if (written != archive.size()) {
        if (errorMessage)
            *errorMessage = QObject::tr("Only %1 of %2 bytes were written.").arg(written).arg(archive.size());
        return false;
    }
    return true;
}

} // namespace xlsx
