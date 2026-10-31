#pragma once
// The game's keyboard actions and key names (live-and-recompiled src/conker/game/keyboard.cpp: kInfo and Named).
// settings.toml stores a key by these names, matched whatever their case.
#include <QString>
#include <QStringList>

class QKeyEvent;
class QMouseEvent;

namespace keys {

struct Action {
    const char* key;    // settings.toml [keys] entry
    const char* label;
    const char* def1;   // defaults
    const char* def2;   // "" none
};

constexpr int kActions = 24;
constexpr int kSlots = 2;
extern const Action kActionList[kActions];

QStringList Defaults(int action);
// the game's name for a key or mouse button, "" when the game cannot bind it
QString FromKey(const QKeyEvent* e);
QString FromMouse(const QMouseEvent* e);

}  // namespace keys
