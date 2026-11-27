#include "officefile.h"

#include <QBuffer>
#include <QDate>
#include <QFileInfo>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QVector>
#include <QXmlStreamReader>

#include <QtGui/private/qzipreader_p.h>
#include <QtGui/private/qzipwriter_p.h>

#include <functional>

namespace Office {

namespace {

const QString kW = QStringLiteral("http://schemas.openxmlformats.org/wordprocessingml/2006/main");
const QString kRel = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships");

QString escapeXml(const QString &s, bool attribute = false)
{
    QString out;
    out.reserve(s.size() + 8);
    for (const QChar c : s) {
        switch (c.unicode()) {
        case '&': out += QLatin1String("&amp;"); break;
        case '<': out += QLatin1String("&lt;"); break;
        case '>': out += QLatin1String("&gt;"); break;
        case '"': if (attribute) { out += QLatin1String("&quot;"); break; } Q_FALLTHROUGH();
        default: out += c;
        }
    }
    return out;
}

// One piece of editable text in the raw XML: the content between a start
// tag and its end tag.
struct Span {
    int tagStart = 0;       // where "<w:t ..." begins
    int contentStart = 0;   // just after its ">"
    int contentEnd = 0;     // where "</w:t>" begins
    QString text;           // decoded
    bool selfClosing = false;
};

// Qt's reader reports its offset after the token just read, but after
// character data it has already looked one character ahead. Offsets taken
// right after a start or end tag are exact; derive everything from those.
struct Tag { int start = 0; int end = 0; bool selfClosing = false; };
Tag tagEndingAt(const QString &raw, int offsetAfterTag)
{
    Tag t;
    t.end = raw.lastIndexOf(QLatin1Char('>'), offsetAfterTag - 1) + 1;
    t.start = raw.lastIndexOf(QLatin1Char('<'), t.end - 1);
    t.selfClosing = t.end >= 2 && raw.at(t.end - 2) == QLatin1Char('/');
    return t;
}

} // namespace

// ---------------------------------------------------------------- package

Kind kindOf(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QLatin1String("docx") || ext == QLatin1String("docm"))
        return Word;
    if (ext == QLatin1String("xlsx") || ext == QLatin1String("xlsm"))
        return Excel;
    return None;
}

QString kindName(Kind k)
{
    return k == Word ? QStringLiteral("Word") : k == Excel ? QStringLiteral("Excel") : QString();
}

bool Package::load(const QByteArray &zip, QString *error)
{
    order.clear();
    files.clear();
    QBuffer buf;
    buf.setData(zip);
    buf.open(QIODevice::ReadOnly);
    QZipReader reader(&buf);
    if (reader.status() != QZipReader::NoError || !reader.isReadable()) {
        *error = QStringLiteral("not a valid Office file (cannot open it as a zip archive)");
        return false;
    }
    for (const QZipReader::FileInfo &fi : reader.fileInfoList()) {
        if (fi.isDir) {
            order << fi.filePath + QLatin1Char('/');
            continue;
        }
        order << fi.filePath;
        files.insert(fi.filePath, reader.fileData(fi.filePath));
    }
    if (reader.status() != QZipReader::NoError) {
        *error = QStringLiteral("the Office file is damaged");
        return false;
    }
    return true;
}

QByteArray Package::save() const
{
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    {
        QZipWriter writer(&buf);
        writer.setCompressionPolicy(QZipWriter::AutoCompress);
        for (const QString &name : order) {
            if (name.endsWith(QLatin1Char('/')))
                writer.addDirectory(name);
            else if (files.contains(name))
                writer.addFile(name, files.value(name));
        }
        writer.close();
    }
    return buf.data();
}

void Package::remove(const QString &name)
{
    files.remove(name);
    order.removeAll(name);
}

QString extractText(const QByteArray &zip, Kind kind, QString *error)
{
    Package p;
    if (!p.load(zip, error))
        return QString();
    return kind == Word ? wordText(p, error) : kind == Excel ? excelText(p, error) : QString();
}

// ------------------------------------------------------------------- Word

QString wordText(const Package &p, QString *error)
{
    if (!p.has(QStringLiteral("word/document.xml"))) {
        *error = QStringLiteral("no word/document.xml: not a Word document");
        return QString();
    }
    QXmlStreamReader x(p.text(QStringLiteral("word/document.xml")));
    QStringList out;
    auto isW = [&](const char *name) {
        return x.namespaceUri() == kW && x.name() == QLatin1String(name);
    };

    std::function<QString()> readParagraph;
    std::function<QStringList()> readTable;

    readParagraph = [&]() -> QString {
        QString text, prefix;
        while (!x.atEnd()) {
            x.readNext();
            if (x.isEndElement() && isW("p"))
                break;
            if (!x.isStartElement())
                continue;
            if (isW("t")) {
                text += x.readElementText();
            } else if (isW("tab")) {
                text += QLatin1Char('\t');
            } else if (isW("br") || isW("cr")) {
                text += QLatin1Char('\n');
            } else if (isW("pStyle")) {
                const QString style = x.attributes().value(kW, QStringLiteral("val")).toString();
                static const QRegularExpression heading(QStringLiteral("^(?:Heading|heading|Titre|berschrift)\\s*([1-9])$"));
                const QRegularExpressionMatch m = heading.match(style);
                if (m.hasMatch())
                    prefix = QString(m.captured(1).toInt(), QLatin1Char('#')) + QLatin1Char(' ');
                else if (style == QLatin1String("Title"))
                    prefix = QStringLiteral("# ");
            } else if (isW("numPr")) {
                if (prefix.isEmpty())
                    prefix = QStringLiteral("- ");
            } else if (isW("p")) {
                text += QLatin1Char(' ') + readParagraph();   // text box inside the paragraph
            } else if (isW("delText") || isW("instrText")) {
                x.skipCurrentElement();                        // deleted text, field codes
            }
        }
        return text.isEmpty() ? QString() : prefix + text;
    };

    readTable = [&]() -> QStringList {
        QStringList rows;
        QStringList cells;
        QStringList cellParts;
        while (!x.atEnd()) {
            x.readNext();
            if (x.isEndElement() && isW("tbl"))
                break;
            if (x.isStartElement() && isW("tr")) {
                cells.clear();
            } else if (x.isEndElement() && isW("tr")) {
                rows << QStringLiteral("| ") + cells.join(QStringLiteral(" | ")) + QStringLiteral(" |");
            } else if (x.isStartElement() && isW("tc")) {
                cellParts.clear();
            } else if (x.isEndElement() && isW("tc")) {
                cells << cellParts.join(QStringLiteral(" / "));
            } else if (x.isStartElement() && isW("p")) {
                const QString t = readParagraph();
                if (!t.isEmpty())
                    cellParts << t;
            } else if (x.isStartElement() && isW("tbl")) {
                cellParts << readTable().join(QStringLiteral("; "));
            }
        }
        return rows;
    };

    while (!x.atEnd()) {
        x.readNext();
        if (!x.isStartElement())
            continue;
        if (isW("p"))
            out << readParagraph();
        else if (isW("tbl"))
            out << readTable();
    }
    if (x.hasError()) {
        *error = QStringLiteral("cannot read the document: ") + x.errorString();
        return QString();
    }
    // Keep empty paragraphs as blank lines, but no runs of them.
    QStringList lines;
    for (const QString &l : out) {
        if (l.isEmpty() && (lines.isEmpty() || lines.last().isEmpty()))
            continue;
        lines << l;
    }
    return lines.join(QLatin1Char('\n'));
}

bool wordReplace(Package &p, const QString &oldTextIn, const QString &newTextIn,
                 QString *error, QString *note)
{
    const QString part = QStringLiteral("word/document.xml");
    if (!p.has(part)) {
        *error = QStringLiteral("not a Word document");
        return false;
    }
    // What the reader shows as tabs and line breaks are separate elements,
    // not text; match on the text alone.
    QString oldText = oldTextIn;
    oldText.remove(QLatin1Char('\t'));
    oldText.remove(QLatin1Char('\n'));
    if (oldText.isEmpty()) {
        *error = QStringLiteral("\"old\" is empty");
        return false;
    }
    if (newTextIn.contains(QLatin1Char('\n'))) {
        *error = QStringLiteral("in a Word document, \"new\" cannot contain line breaks: change one paragraph at a time");
        return false;
    }
    QString newText = newTextIn;
    newText.replace(QLatin1Char('\t'), QLatin1Char(' '));

    QString raw = p.text(part);
    QXmlStreamReader x(raw);
    QVector<QVector<Span>> paragraphs;
    QVector<QVector<Span>> open;   // nested paragraphs (text boxes)
    while (!x.atEnd()) {
        x.readNext();
        if (x.namespaceUri() != kW)
            continue;
        if (x.isStartElement() && x.name() == QLatin1String("p")) {
            open.append(QVector<Span>());
        } else if (x.isEndElement() && x.name() == QLatin1String("p")) {
            if (!open.isEmpty())
                paragraphs.append(open.takeLast());
        } else if (x.isStartElement() && x.name() == QLatin1String("t") && !open.isEmpty()) {
            const Tag tag = tagEndingAt(raw, int(x.characterOffset()));
            Span s;
            s.tagStart = tag.start;
            s.contentStart = tag.end;
            s.selfClosing = tag.selfClosing;
            // Text cannot contain a raw "<", so the end tag is the next "</".
            s.contentEnd = s.selfClosing ? s.contentStart : raw.indexOf(QLatin1String("</"), s.contentStart);
            s.text = x.readElementText();
            if (s.contentEnd < 0) {
                *error = QStringLiteral("cannot read the document structure");
                return false;
            }
            open.last().append(s);
        }
    }
    if (x.hasError()) {
        *error = QStringLiteral("cannot read the document: ") + x.errorString();
        return false;
    }

    // Exactly one occurrence, inside one paragraph.
    int foundPara = -1, foundPos = -1, count = 0;
    for (int i = 0; i < paragraphs.size(); ++i) {
        QString joined;
        for (const Span &s : paragraphs[i])
            joined += s.text;
        int from = 0, pos;
        while ((pos = joined.indexOf(oldText, from)) >= 0) {
            if (++count == 1) {
                foundPara = i;
                foundPos = pos;
            }
            from = pos + 1;
        }
    }
    if (count == 0) {
        *error = QStringLiteral("\"old\" text not found in the document. It must be within one paragraph; "
                                "read the document again and copy the text exactly");
        return false;
    }
    if (count > 1) {
        *error = QStringLiteral("\"old\" text occurs %1 times; include more of the sentence so it is unique").arg(count);
        return false;
    }

    // Spread the change over the text pieces it touches: the first keeps
    // its formatting and receives the new text, later ones lose the part
    // that was replaced.
    const QVector<Span> &spans = paragraphs[foundPara];
    const int endPos = foundPos + oldText.size();
    struct Change { int span; QString text; };
    QVector<Change> changes;
    int offset = 0, touched = 0;
    bool placed = false;
    for (int i = 0; i < spans.size(); ++i) {
        const int a = offset, b = offset + spans[i].text.size();
        offset = b;
        if (b <= foundPos || a >= endPos || spans[i].text.isEmpty())
            continue;
        ++touched;
        const QString &t = spans[i].text;
        const QString keepBefore = t.left(qMax(0, foundPos - a));
        const QString keepAfter = endPos < b ? t.mid(endPos - a) : QString();
        changes.append({i, keepBefore + (placed ? QString() : newText) + keepAfter});
        placed = true;
    }
    for (const Change &c : changes)
        if (spans[c.span].selfClosing) {
            *error = QStringLiteral("unexpected document structure around that text");
            return false;
        }

    // Apply from the end so earlier offsets stay valid.
    for (int k = changes.size() - 1; k >= 0; --k) {
        const Span &s = spans[changes[k].span];
        const QString &t = changes[k].text;
        raw.replace(s.contentStart, s.contentEnd - s.contentStart, escapeXml(t));
        // Word drops leading/trailing spaces unless the element says to keep them.
        const QString tag = raw.mid(s.tagStart, s.contentStart - s.tagStart);
        const bool edgeSpace = !t.isEmpty() && (t.at(0).isSpace() || t.at(t.size() - 1).isSpace());
        if (edgeSpace && !tag.contains(QLatin1String("xml:space"))) {
            QString newTag = tag;
            newTag.insert(newTag.size() - 1, QStringLiteral(" xml:space=\"preserve\""));
            raw.replace(s.tagStart, tag.size(), newTag);
        }
    }
    p.setText(part, raw);
    if (touched > 1)
        *note = QStringLiteral("the old text spanned %1 differently formatted parts; the new text has the formatting of the first")
                    .arg(touched);
    return true;
}

// ------------------------------------------------------------------ Excel

namespace {

struct SheetRef { QString name; QString part; };

QString resolveTarget(const QString &target)
{
    if (target.startsWith(QLatin1Char('/')))
        return target.mid(1);
    QString t = QStringLiteral("xl/") + target;
    while (t.contains(QLatin1String("/../"))) {
        const int i = t.indexOf(QLatin1String("/../"));
        const int j = t.lastIndexOf(QLatin1Char('/'), i - 1);
        t.remove(j + 1, i + 4 - (j + 1));
    }
    return t;
}

QVector<SheetRef> sheetsOf(const Package &p, QString *error)
{
    QHash<QString, QString> rels;
    {
        QXmlStreamReader x(p.text(QStringLiteral("xl/_rels/workbook.xml.rels")));
        while (!x.atEnd()) {
            x.readNext();
            if (x.isStartElement() && x.name() == QLatin1String("Relationship"))
                rels.insert(x.attributes().value(QStringLiteral("Id")).toString(),
                            resolveTarget(x.attributes().value(QStringLiteral("Target")).toString()));
        }
    }
    QVector<SheetRef> sheets;
    QXmlStreamReader x(p.text(QStringLiteral("xl/workbook.xml")));
    while (!x.atEnd()) {
        x.readNext();
        if (x.isStartElement() && x.name() == QLatin1String("sheet")) {
            const QString id = x.attributes().value(kRel, QStringLiteral("id")).toString();
            const QString part = rels.value(id);
            if (p.has(part))   // chart sheets and the like have no cells: skip
                sheets.append({x.attributes().value(QStringLiteral("name")).toString(), part});
        }
    }
    if (sheets.isEmpty())
        *error = QStringLiteral("no worksheets found: not an Excel workbook");
    return sheets;
}

QStringList sharedStrings(const Package &p)
{
    QStringList out;
    const QString part = QStringLiteral("xl/sharedStrings.xml");
    if (!p.has(part))
        return out;
    QXmlStreamReader x(p.text(part));
    QString current;
    bool inSi = false;
    while (!x.atEnd()) {
        x.readNext();
        if (x.isStartElement() && x.name() == QLatin1String("si")) {
            inSi = true;
            current.clear();
        } else if (x.isEndElement() && x.name() == QLatin1String("si")) {
            inSi = false;
            out << current;
        } else if (x.isStartElement() && x.name() == QLatin1String("rPh")) {
            x.skipCurrentElement();            // phonetic hints, not cell text
        } else if (inSi && x.isStartElement() && x.name() == QLatin1String("t")) {
            current += x.readElementText();
        }
    }
    return out;
}

// For each cell style index: whether its number format is a date.
QVector<bool> dateStyles(const Package &p)
{
    QVector<bool> out;
    const QString part = QStringLiteral("xl/styles.xml");
    if (!p.has(part))
        return out;
    QHash<int, QString> custom;
    QXmlStreamReader x(p.text(part));
    bool inCellXfs = false;
    QVector<int> fmtIds;
    while (!x.atEnd()) {
        x.readNext();
        if (x.isStartElement() && x.name() == QLatin1String("numFmt"))
            custom.insert(x.attributes().value(QStringLiteral("numFmtId")).toInt(),
                          x.attributes().value(QStringLiteral("formatCode")).toString());
        else if (x.isStartElement() && x.name() == QLatin1String("cellXfs"))
            inCellXfs = true;
        else if (x.isEndElement() && x.name() == QLatin1String("cellXfs"))
            inCellXfs = false;
        else if (inCellXfs && x.isStartElement() && x.name() == QLatin1String("xf"))
            fmtIds << x.attributes().value(QStringLiteral("numFmtId")).toInt();
    }
    for (int id : fmtIds) {
        bool date = (id >= 14 && id <= 22) || (id >= 45 && id <= 47);
        if (custom.contains(id)) {
            QString code = custom.value(id);
            code.remove(QRegularExpression(QStringLiteral("\"[^\"]*\"|\\[[^\\]]*\\]|\\\\.")));
            date = code.contains(QLatin1Char('y'), Qt::CaseInsensitive)
                || code.contains(QLatin1Char('d'), Qt::CaseInsensitive);
        }
        out << date;
    }
    return out;
}

QString serialToDate(double serial)
{
    const QDate d = QDate(1899, 12, 30).addDays(qint64(serial));
    const double frac = serial - qint64(serial);
    if (frac <= 0.0)
        return d.toString(Qt::ISODate);
    const int secs = int(frac * 86400.0 + 0.5);
    return d.toString(Qt::ISODate) + QStringLiteral(" %1:%2").arg(secs / 3600, 2, 10, QLatin1Char('0'))
                                                            .arg((secs / 60) % 60, 2, 10, QLatin1Char('0'));
}

int columnIndex(const QString &ref)
{
    int n = 0;
    for (const QChar c : ref) {
        if (!c.isLetter())
            break;
        n = n * 26 + (c.toUpper().unicode() - 'A' + 1);
    }
    return n;
}

int rowIndex(const QString &ref)
{
    int i = 0;
    while (i < ref.size() && ref.at(i).isLetter())
        ++i;
    return ref.mid(i).toInt();
}

} // namespace

QString excelText(const Package &p, QString *error)
{
    const QVector<SheetRef> sheets = sheetsOf(p, error);
    if (sheets.isEmpty())
        return QString();
    const QStringList shared = sharedStrings(p);
    const QVector<bool> dates = dateStyles(p);
    QStringList out;
    for (const SheetRef &sheet : sheets) {
        if (!out.isEmpty())
            out << QString();
        out << QStringLiteral("# Sheet \"%1\"").arg(sheet.name);
        const int headerLine = int(out.size());
        QXmlStreamReader x(p.text(sheet.part));
        while (!x.atEnd()) {
            x.readNext();
            if (!(x.isStartElement() && x.name() == QLatin1String("c")))
                continue;
            const QString ref = x.attributes().value(QStringLiteral("r")).toString();
            const QString type = x.attributes().value(QStringLiteral("t")).toString();
            const int style = x.attributes().value(QStringLiteral("s")).toInt();
            QString v, f, inlineText;
            bool hasF = false, sharedF = false;
            while (!x.atEnd()) {
                x.readNext();
                if (x.isEndElement() && x.name() == QLatin1String("c"))
                    break;
                if (!x.isStartElement())
                    continue;
                if (x.name() == QLatin1String("v")) {
                    v = x.readElementText();
                } else if (x.name() == QLatin1String("f")) {
                    hasF = true;
                    sharedF = x.attributes().value(QStringLiteral("t")) == QLatin1String("shared");
                    f = x.readElementText();
                } else if (x.name() == QLatin1String("t")) {
                    inlineText += x.readElementText();
                }
            }
            QString shown;
            if (type == QLatin1String("s"))
                shown = shared.value(v.toInt());
            else if (type == QLatin1String("inlineStr"))
                shown = inlineText;
            else if (type == QLatin1String("b"))
                shown = v == QLatin1String("1") ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
            else if (!v.isEmpty() && (type.isEmpty() || type == QLatin1String("n"))
                     && style < dates.size() && dates.at(style))
                shown = serialToDate(v.toDouble());
            else
                shown = v;
            shown.replace(QLatin1Char('\n'), QLatin1String("\\n"));
            if (hasF) {
                const QString formula = f.isEmpty() && sharedF ? QStringLiteral("(shared formula)") : QLatin1Char('=') + f;
                out << (shown.isEmpty() ? QStringLiteral("%1 = %2").arg(ref, formula)
                                        : QStringLiteral("%1 = %2   [value: %3]").arg(ref, formula, shown));
            } else if (!shown.isEmpty()) {
                out << QStringLiteral("%1 = %2").arg(ref, shown);
            }
        }
        if (out.size() == headerLine)
            out << QStringLiteral("(empty)");
    }
    return out.join(QLatin1Char('\n'));
}

namespace {

struct CellPos { QString ref; int col = 0; int start = 0; int end = 0; QString style; };
struct RowPos {
    int num = 0;
    int start = 0, contentStart = 0, contentEnd = 0, end = 0;
    bool selfClosing = false;
    QVector<CellPos> cells;
};
struct SheetLayout {
    bool found = false;
    int start = 0, contentStart = 0, contentEnd = 0, end = 0;
    bool selfClosing = false;
    QVector<RowPos> rows;
};

SheetLayout layoutOf(const QString &raw)
{
    SheetLayout L;
    QXmlStreamReader x(raw);
    RowPos row;
    bool inRow = false;
    CellPos cell;
    int depthInCell = 0;
    while (!x.atEnd()) {
        x.readNext();
        if (!x.isStartElement() && !x.isEndElement())
            continue;
        const Tag tag = tagEndingAt(raw, int(x.characterOffset()));
        if (x.isStartElement()) {
            if (depthInCell > 0) {
                ++depthInCell;
                continue;
            }
            if (x.name() == QLatin1String("sheetData")) {
                L.found = true;
                L.start = tag.start;
                L.contentStart = tag.end;
                L.selfClosing = tag.selfClosing;
            } else if (x.name() == QLatin1String("row")) {
                row = RowPos();
                row.num = x.attributes().value(QStringLiteral("r")).toInt();
                row.start = tag.start;
                row.contentStart = tag.end;
                row.selfClosing = tag.selfClosing;
                inRow = true;
            } else if (x.name() == QLatin1String("c") && inRow) {
                cell = CellPos();
                cell.ref = x.attributes().value(QStringLiteral("r")).toString();
                cell.col = columnIndex(cell.ref);
                cell.start = tag.start;
                cell.style = x.attributes().value(QStringLiteral("s")).toString();
                depthInCell = 1;
            }
        } else {
            // For a self-closing element the end "tag" is the start tag itself.
            if (depthInCell > 1) {
                --depthInCell;
                continue;
            }
            if (depthInCell == 1 && x.name() == QLatin1String("c")) {
                cell.end = tag.end;
                row.cells.append(cell);
                depthInCell = 0;
            } else if (x.name() == QLatin1String("row") && inRow) {
                row.contentEnd = row.selfClosing ? row.contentStart : tag.start;
                row.end = tag.end;
                L.rows.append(row);
                inRow = false;
            } else if (x.name() == QLatin1String("sheetData")) {
                L.contentEnd = L.selfClosing ? L.contentStart : tag.start;
                L.end = tag.end;
            }
        }
    }
    return L;
}

bool cellXml(const QString &ref, const QString &style, const QJsonValue &value, QString *xml, QString *error)
{
    const QString s = style.isEmpty() ? QString() : QStringLiteral(" s=\"%1\"").arg(escapeXml(style, true));
    switch (value.type()) {
    case QJsonValue::Null:
        *xml = QStringLiteral("<c r=\"%1\"%2/>").arg(ref, s);
        return true;
    case QJsonValue::Bool:
        *xml = QStringLiteral("<c r=\"%1\"%2 t=\"b\"><v>%3</v></c>").arg(ref, s, value.toBool() ? QStringLiteral("1") : QStringLiteral("0"));
        return true;
    case QJsonValue::Double:
        *xml = QStringLiteral("<c r=\"%1\"%2><v>%3</v></c>").arg(ref, s, QString::number(value.toDouble(), 'g', 15));
        return true;
    case QJsonValue::String: {
        const QString t = value.toString();
        if (t.startsWith(QLatin1Char('=')) && t.size() > 1)
            *xml = QStringLiteral("<c r=\"%1\"%2><f>%3</f></c>").arg(ref, s, escapeXml(t.mid(1)));
        else
            *xml = QStringLiteral("<c r=\"%1\"%2 t=\"inlineStr\"><is><t xml:space=\"preserve\">%3</t></is></c>")
                       .arg(ref, s, escapeXml(t));
        return true;
    }
    default:
        *error = QStringLiteral("value for %1 must be text, a number, true/false, a \"=formula\" or null").arg(ref);
        return false;
    }
}

// Excel keeps a list of formula cells in calculation order; after edits it
// can be wrong, and a wrong one makes Excel "repair" the file. Without it,
// and with full calculation on load, Excel rebuilds it.
void forceRecalc(Package &p)
{
    // Formula cells cache their last result. After an edit those can be
    // wrong, and programs that show cached results (previews, LibreOffice
    // with default settings) would show stale numbers. Drop them in every
    // sheet, since formulas can refer to other sheets.
    QString ignored;
    static const QRegularExpression cellRe(QStringLiteral("<c\\b[^>]*>(.*?)</c>"),
                                           QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression cachedRe(QStringLiteral("<v>[^<]*</v>|<v/>"));
    for (const SheetRef &sheet : sheetsOf(p, &ignored)) {
        const QString raw = p.text(sheet.part);
        QString out;
        out.reserve(raw.size());
        int last = 0;
        auto it = cellRe.globalMatch(raw);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            QString inner = m.captured(1);
            if (!inner.contains(QLatin1String("<f")))
                continue;
            inner.remove(cachedRe);
            out += raw.midRef(last, m.capturedStart(1) - last);
            out += inner;
            last = m.capturedEnd(1);
        }
        if (last > 0) {
            out += raw.midRef(last);
            p.setText(sheet.part, out);
        }
    }

    if (p.has(QStringLiteral("xl/calcChain.xml"))) {
        p.remove(QStringLiteral("xl/calcChain.xml"));
        QString ct = p.text(QStringLiteral("[Content_Types].xml"));
        ct.remove(QRegularExpression(QStringLiteral("<Override[^>]*PartName=\"/xl/calcChain.xml\"[^>]*/>")));
        p.setText(QStringLiteral("[Content_Types].xml"), ct);
        QString rels = p.text(QStringLiteral("xl/_rels/workbook.xml.rels"));
        rels.remove(QRegularExpression(QStringLiteral("<Relationship[^>]*Target=\"[^\"]*calcChain.xml\"[^>]*/>")));
        p.setText(QStringLiteral("xl/_rels/workbook.xml.rels"), rels);
    }
    QString wb = p.text(QStringLiteral("xl/workbook.xml"));
    const QRegularExpression calcPr(QStringLiteral("<calcPr\\b([^>]*?)(/?)>"));
    const QRegularExpressionMatch m = calcPr.match(wb);
    if (m.hasMatch()) {
        QString attrs = m.captured(1);
        attrs.remove(QRegularExpression(QStringLiteral("\\s+fullCalcOnLoad=\"[^\"]*\"")));
        wb.replace(m.capturedStart(), m.capturedLength(),
                   QStringLiteral("<calcPr%1 fullCalcOnLoad=\"1\"%2>").arg(attrs, m.captured(2)));
    } else {
        int at = -1;
        for (const QString &anchor : {QStringLiteral("</definedNames>"), QStringLiteral("<definedNames/>"),
                                      QStringLiteral("</sheets>")}) {
            const int i = wb.indexOf(anchor);
            if (i >= 0) {
                at = i + anchor.size();
                break;
            }
        }
        if (at >= 0)
            wb.insert(at, QStringLiteral("<calcPr fullCalcOnLoad=\"1\"/>"));
    }
    p.setText(QStringLiteral("xl/workbook.xml"), wb);
}

} // namespace

bool excelSetCells(Package &p, const QString &sheetName, const QJsonObject &cells,
                   QString *error, QString *usedSheet)
{
    const QVector<SheetRef> sheets = sheetsOf(p, error);
    if (sheets.isEmpty())
        return false;
    if (cells.isEmpty()) {
        *error = QStringLiteral("\"cells\" is empty");
        return false;
    }
    if (cells.size() > 500) {
        *error = QStringLiteral("at most 500 cells per step");
        return false;
    }
    SheetRef target;
    if (sheetName.isEmpty()) {
        target = sheets.first();
    } else {
        for (const SheetRef &s : sheets)
            if (s.name.compare(sheetName, Qt::CaseInsensitive) == 0)
                target = s;
        if (target.part.isEmpty()) {
            QStringList names;
            for (const SheetRef &s : sheets)
                names << QLatin1Char('"') + s.name + QLatin1Char('"');
            *error = QStringLiteral("no sheet named \"%1\"; the sheets are %2").arg(sheetName, names.join(QStringLiteral(", ")));
            return false;
        }
    }
    *usedSheet = target.name;

    static const QRegularExpression refRe(QStringLiteral("^\\$?([A-Za-z]{1,3})\\$?([0-9]{1,7})$"));
    QString raw = p.text(target.part);
    for (auto it = cells.begin(); it != cells.end(); ++it) {
        const QRegularExpressionMatch m = refRe.match(it.key().trimmed());
        if (!m.hasMatch() || m.captured(2).toInt() < 1) {
            *error = QStringLiteral("\"%1\" is not a cell reference like B2").arg(it.key());
            return false;
        }
        const QString ref = m.captured(1).toUpper() + m.captured(2);
        const int col = columnIndex(ref), rowNum = rowIndex(ref);

        const SheetLayout L = layoutOf(raw);
        if (!L.found) {
            *error = QStringLiteral("the sheet has no cell data section");
            return false;
        }
        const RowPos *row = nullptr;
        const RowPos *nextRow = nullptr;
        for (const RowPos &r : L.rows) {
            if (r.num == rowNum)
                row = &r;
            else if (r.num > rowNum && !nextRow)
                nextRow = &r;
        }
        const CellPos *existing = nullptr;
        const CellPos *nextCell = nullptr;
        if (row) {
            for (const CellPos &c : row->cells) {
                if (c.ref.compare(ref, Qt::CaseInsensitive) == 0)
                    existing = &c;
                else if (c.col > col && !nextCell)
                    nextCell = &c;
            }
        }
        QString xml;
        if (!cellXml(ref, existing ? existing->style : QString(), it.value(), &xml, error))
            return false;

        if (existing) {
            raw.replace(existing->start, existing->end - existing->start, xml);
        } else if (row && !row->selfClosing) {
            raw.insert(nextCell ? nextCell->start : row->contentEnd, xml);
        } else if (row) {
            QString tag = raw.mid(row->start, row->end - row->start);
            tag.chop(2);                                   // drop "/>"
            raw.replace(row->start, row->end - row->start, tag + QLatin1Char('>') + xml + QStringLiteral("</row>"));
        } else {
            const QString newRow = QStringLiteral("<row r=\"%1\">%2</row>").arg(rowNum).arg(xml);
            if (nextRow) {
                raw.insert(nextRow->start, newRow);
            } else if (!L.selfClosing) {
                raw.insert(L.contentEnd, newRow);
            } else {
                QString tag = raw.mid(L.start, L.end - L.start);
                tag.chop(2);
                raw.replace(L.start, L.end - L.start, tag + QLatin1Char('>') + newRow + QStringLiteral("</sheetData>"));
            }
        }
    }
    p.setText(target.part, raw);
    forceRecalc(p);
    return true;
}

} // namespace Office
