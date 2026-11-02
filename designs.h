#pragma once

#include <QSize>

class QWidget;

// Each factory returns a fully styled top-level window (style sheet is scoped to that window only).
QWidget *makeAurora();       // dark analytics SaaS dashboard
QWidget *makeVault();        // light fintech / banking app
QWidget *makeControlRoom();  // industrial HMI / SCADA panel

inline QSize designSize() { return {1360, 860}; }
