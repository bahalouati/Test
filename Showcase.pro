# qmake project file ---------------------------------------------------------
QT       += core gui widgets      # Qt modules we link against
CONFIG   += c++17
TARGET    = Showcase
TEMPLATE  = app

SOURCES  += main.cpp designs.cpp widgets.cpp form_widgets.cpp guidedform.cpp
HEADERS  += designs.h widgets.h form_widgets.h guidedform.h qssloader.h

# Qt Designer form. qmake runs uic on it and generates ui_guidedform.h (class Ui::GuidedForm) in the build
# folder. Open guidedform.ui in Qt Designer / Qt Creator to edit the layout visually.
FORMS    += guidedform.ui

# The style sheets (themes/*.qss) are compiled into the executable through this
# resource file. Editing a .qss therefore needs a rebuild, UNLESS you run with the
# QSS_DIR environment variable pointing at the themes folder (see qssloader.h).
RESOURCES += resources.qrc
