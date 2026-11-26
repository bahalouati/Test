#pragma once

class CopilotBridge;
class QWebEnginePage;

// Lets the bridge drive a page shown in Qt's built-in WebEngine. Scripts run
// in an isolated world: same DOM, none of the page's own JS globals.
void attachWebEnginePage(CopilotBridge *bridge, QWebEnginePage *page);
