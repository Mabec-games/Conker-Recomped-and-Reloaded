#include "paths.h"

#include <QDir>
#include <QFileInfo>

namespace paths {

namespace {

constexpr const char* kFolder = "LiveAndRecompiled";

QString ExeDir(const QString& gameExe) { return QFileInfo(gameExe).absolutePath(); }

bool Portable(const QString& gameExe) { return QFileInfo::exists(ExeDir(gameExe) + "/portable.txt"); }

// kind 0 config, 1 data (the game's ConfigDir and DataDir)
QString Dir(const QString& gameExe, int kind) {
    if (Portable(gameExe)) return ExeDir(gameExe);
#ifdef Q_OS_WIN
    Q_UNUSED(kind);
    const QString base = qEnvironmentVariable("APPDATA");  // both are Roaming on Windows
#else
    const char* var = kind == 0 ? "XDG_CONFIG_HOME" : "XDG_DATA_HOME";
    const char* fallback = kind == 0 ? ".config" : ".local/share";
    QString base = qEnvironmentVariable(var);
    if (!base.startsWith('/')) base = QDir::homePath() + "/" + fallback;  // the spec ignores a relative one
#endif
    if (base.isEmpty()) return ExeDir(gameExe);
    return QDir::cleanPath(base + "/" + kFolder);
}

}  // namespace

QString ConfigDir(const QString& gameExe) { return Dir(gameExe, 0); }
QString DataDir(const QString& gameExe) { return Dir(gameExe, 1); }
QString SettingsFile(const QString& gameExe) { return ConfigDir(gameExe) + "/settings.toml"; }
QString CacheDrive(const QString& gameExe) { return DataDir(gameExe) + "/hdd/Z"; }

QString FindAssets(const QString& gameExe) {
    QDir dir(ExeDir(gameExe));
    for (int i = 0; i < 6; i++) {
        if (QFileInfo(dir.filePath("assets")).isDir()) return QDir::cleanPath(dir.filePath("assets"));
        if (!dir.cdUp()) break;
    }
    return {};
}

bool AssetsExtracted(const QString& assets) {
    return !assets.isEmpty() && QFileInfo::exists(assets + "/default.xbe");
}

QString DefaultGameName() {
#ifdef Q_OS_WIN
    return "Game.exe";
#else
    return "Game";
#endif
}

}  // namespace paths
