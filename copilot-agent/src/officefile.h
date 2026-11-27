#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// Word (.docx/.docm) and Excel (.xlsx/.xlsm) files: their text for reading
// and searching, and the two edits the agent may make:
//   - Word: replace one exact piece of text inside a paragraph;
//   - Excel: set the values or formulas of given cells.
// Edits change only the bytes of the affected text or cells inside the
// document's XML. Everything else in the file (styles, images, layout,
// other sheets, macros) is copied as it was.
namespace Office {

enum Kind { None, Word, Excel };
Kind kindOf(const QString &path);
QString kindName(Kind k);

// The files inside a .docx/.xlsx (a zip archive), in their original order.
struct Package {
    QStringList order;
    QHash<QString, QByteArray> files;
    bool load(const QByteArray &zip, QString *error);
    QByteArray save() const;
    bool has(const QString &name) const { return files.contains(name); }
    QString text(const QString &name) const { return QString::fromUtf8(files.value(name)); }
    void setText(const QString &name, const QString &xml) { files[name] = xml.toUtf8(); }
    void remove(const QString &name);
};

// Readable text: Word as paragraphs (headings as "#", tables as "| a | b |");
// Excel as one "A1 = value" line per cell, sheet by sheet, formulas with
// their last calculated value.
QString wordText(const Package &p, QString *error);
QString excelText(const Package &p, QString *error);
QString extractText(const QByteArray &zip, Kind kind, QString *error);

// Replaces the single occurrence of oldText (within one paragraph). If it
// spans differently formatted parts, the new text takes the formatting of
// the first part and *note says so.
bool wordReplace(Package &p, const QString &oldText, const QString &newText,
                 QString *error, QString *note);

// cells: {"B2": "text", "C3": 42, "D4": "=SUM(A1:A3)", "E5": true, "F6": null}.
// A string starting with "=" is a formula, null empties the cell. Cell
// styles are kept. Excel recalculates formulas when the file is opened.
bool excelSetCells(Package &p, const QString &sheet, const QJsonObject &cells,
                   QString *error, QString *usedSheet);

} // namespace Office
