#pragma once

#include <QDir>
#include <QFile>
#include <QString>

// Returns the style sheet text for a design (the file themes/<name>.qss).
//
// Normal run:   read from the Qt resource system (":/themes/<name>.qss"), i.e. the copy that was
//               compiled into the executable through resources.qrc.
// Live editing: if the environment variable QSS_DIR points at a folder that contains <name>.qss,
//               that file is read from disk instead. Edit, restart, see the change - no recompiling:
//                   QSS_DIR=/home/me/qt_showcase/themes ./Showcase --design form
inline QString loadQss(const QString &name)
{
    const QString dir = qEnvironmentVariable("QSS_DIR");
    const QString diskPath = dir.isEmpty() ? QString() : QDir(dir).filePath(name + ".qss");
    QFile f(!diskPath.isEmpty() && QFile::exists(diskPath) ? diskPath : ":/themes/" + name + ".qss");
    if (!f.open(QFile::ReadOnly | QFile::Text)) {
        qWarning("Could not open style sheet '%s'", qPrintable(f.fileName()));
        return QString();
    }
    return QString::fromUtf8(f.readAll());
}
