#include "keys.h"

#include <QKeyEvent>
#include <QMouseEvent>

#ifdef Q_OS_WIN
#include <windows.h>
#define VK_RSHIFT_COMPAT VK_RSHIFT
#define VK_RCONTROL_COMPAT VK_RCONTROL
#define VK_RMENU_COMPAT VK_RMENU
#else
#define VK_RSHIFT_COMPAT 0
#define VK_RCONTROL_COMPAT 0
#define VK_RMENU_COMPAT 0
#endif

namespace keys {

const Action kActionList[kActions] = {
    {"move_up", "Move up", "W", ""},           {"move_down", "Move down", "S", ""},
    {"move_left", "Move left", "A", ""},       {"move_right", "Move right", "D", ""},
    {"camera_up", "Camera up", "Up", ""},      {"camera_down", "Camera down", "Down", ""},
    {"camera_left", "Camera left", "Left", ""}, {"camera_right", "Camera right", "Right", ""},
    {"dpad_up", "D-pad up", "1", ""},          {"dpad_down", "D-pad down", "3", ""},
    {"dpad_left", "D-pad left", "4", ""},      {"dpad_right", "D-pad right", "2", ""},
    {"a", "A", "Space", ""},                   {"b", "B", "E", "Mouse 2"},
    {"x", "X", "R", ""},                       {"y", "Y", "Tab", ""},
    {"lb", "LB (white)", "T", ""},             {"rb", "RB (black)", "K", ""},
    {"lt", "LT", "Q", "Mouse 3"},              {"rt", "RT", "Left Ctrl", "Mouse 1"},
    {"start", "Start", "Enter", ""},           {"back", "Back", "Backspace", ""},
    {"ls", "Left stick click", "Left Shift", ""}, {"rs", "Right stick click", "Left Alt", ""},
};

QStringList Defaults(int action) {
    QStringList out;
    out << kActionList[action].def1;
    if (*kActionList[action].def2) out << kActionList[action].def2;
    return out;
}

namespace {

// left or right of a modifier pair, from the physical key
bool RightSide(const QKeyEvent* e, int linuxRight, int winVk) {
#ifdef Q_OS_WIN
    Q_UNUSED(e);
    Q_UNUSED(linuxRight);
    return GetAsyncKeyState(winVk) & 0x8000;
#else
    Q_UNUSED(winVk);
    return int(e->nativeScanCode()) == linuxRight;  // evdev code + 8, as X11 and Wayland report it
#endif
}

}  // namespace

QString FromKey(const QKeyEvent* e) {
    const int k = e->key();
    const bool pad = e->modifiers() & Qt::KeypadModifier;
    if (k >= Qt::Key_A && k <= Qt::Key_Z) return QString(QChar('A' + (k - Qt::Key_A)));
    if (k >= Qt::Key_0 && k <= Qt::Key_9) {
        const QString digit(QChar('0' + (k - Qt::Key_0)));
        return pad ? "Numpad " + digit : digit;
    }
    if (k >= Qt::Key_F2 && k <= Qt::Key_F12) return "F" + QString::number(k - Qt::Key_F1 + 1);  // F1 is reserved
    if (pad) {
        switch (k) {
            case Qt::Key_Asterisk: return "Numpad *";
            case Qt::Key_Plus: return "Numpad +";
            case Qt::Key_Minus: return "Numpad -";
            case Qt::Key_Period: case Qt::Key_Comma: return "Numpad .";
            case Qt::Key_Slash: return "Numpad /";
            case Qt::Key_Enter: return "Enter";
            default: break;
        }
    }
    switch (k) {
        case Qt::Key_Backspace: return "Backspace";
        case Qt::Key_Tab: case Qt::Key_Backtab: return "Tab";
        case Qt::Key_Return: case Qt::Key_Enter: return "Enter";
        case Qt::Key_CapsLock: return "Caps Lock";
        case Qt::Key_Space: return "Space";
        case Qt::Key_PageUp: return "Page Up";
        case Qt::Key_PageDown: return "Page Down";
        case Qt::Key_End: return "End";
        case Qt::Key_Home: return "Home";
        case Qt::Key_Left: return "Left";
        case Qt::Key_Up: return "Up";
        case Qt::Key_Right: return "Right";
        case Qt::Key_Down: return "Down";
        case Qt::Key_Insert: return "Insert";
        case Qt::Key_Delete: return "Delete";
        case Qt::Key_Shift: return RightSide(e, 62, VK_RSHIFT_COMPAT) ? "Right Shift" : "Left Shift";
        case Qt::Key_Control: return RightSide(e, 105, VK_RCONTROL_COMPAT) ? "Right Ctrl" : "Left Ctrl";
        case Qt::Key_Alt: return RightSide(e, 108, VK_RMENU_COMPAT) ? "Right Alt" : "Left Alt";
        case Qt::Key_AltGr: return "Right Alt";
        // US layout punctuation, shifted or not
        case Qt::Key_Semicolon: case Qt::Key_Colon: return ";";
        case Qt::Key_Equal: case Qt::Key_Plus: return "=";
        case Qt::Key_Comma: case Qt::Key_Less: return ",";
        case Qt::Key_Minus: case Qt::Key_Underscore: return "-";
        case Qt::Key_Period: case Qt::Key_Greater: return ".";
        case Qt::Key_Slash: case Qt::Key_Question: return "/";
        case Qt::Key_QuoteLeft: case Qt::Key_AsciiTilde: return "`";
        case Qt::Key_BracketLeft: case Qt::Key_BraceLeft: return "[";
        case Qt::Key_Backslash: case Qt::Key_Bar: return "\\";
        case Qt::Key_BracketRight: case Qt::Key_BraceRight: return "]";
        case Qt::Key_Apostrophe: case Qt::Key_QuoteDbl: return "'";
        // shifted digits
        case Qt::Key_Exclam: return "1";
        case Qt::Key_At: return "2";
        case Qt::Key_NumberSign: return "3";
        case Qt::Key_Dollar: return "4";
        case Qt::Key_Percent: return "5";
        case Qt::Key_AsciiCircum: return "6";
        case Qt::Key_Ampersand: return "7";
        case Qt::Key_Asterisk: return "8";
        case Qt::Key_ParenLeft: return "9";
        case Qt::Key_ParenRight: return "0";
        default: return {};
    }
}

QString FromMouse(const QMouseEvent* e) {
    switch (e->button()) {
        case Qt::LeftButton: return "Mouse 1";
        case Qt::RightButton: return "Mouse 2";
        case Qt::MiddleButton: return "Mouse 3";
        case Qt::BackButton: return "Mouse 4";
        case Qt::ForwardButton: return "Mouse 5";
        default: return {};
    }
}

}  // namespace keys
