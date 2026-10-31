#pragma once
// Where the game keeps its files. Mirrors live-and-recompiled src/runtime/app/paths.cpp and game_files.cpp:
// a portable.txt beside the game puts everything beside it; otherwise %APPDATA% on Windows and the XDG folders
// elsewhere, each with a LiveAndRecompiled folder. The game finds assets/ by walking up from its own folder.
#include <QString>

namespace paths {

QString ConfigDir(const QString& gameExe);    // holds settings.toml
QString DataDir(const QString& gameExe);      // holds hdd/ (E: saves, Z: cache)
QString SettingsFile(const QString& gameExe);
QString CacheDrive(const QString& gameExe);   // hdd/Z, the game's cache partition
QString FindAssets(const QString& gameExe);   // "" when none
bool AssetsExtracted(const QString& assets);  // default.xbe in it (not only a disc image)
QString DefaultGameName();                    // "Game.exe" or "Game"

}  // namespace paths
