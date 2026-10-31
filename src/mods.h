#pragma once
// Mods are folders laid out like the game's files (dvddata/..., media/...), each in the mods folder. Applying
// one replaces the game's files with its own in the extracted assets folder, after moving each original aside into
// assets/.launcher-mods/original. Turning it off puts the originals back. The disc image is never touched: mods need
// the extracted files, which the game reads before any image.
#include <QString>
#include <QStringList>

#include <functional>

namespace mods {

struct Mod {
    QString name;         // its folder's name
    QString path;
    QString description;  // mod.txt, if any
    int files = 0;
};

QList<Mod> List(const QString& modsDir);
// the mods applied now, from the manifest
QStringList Applied(const QString& assets);

struct Result {
    bool ok = true;
    bool changed = false;  // any game file replaced or restored
    QString error;
    int replaced = 0, restored = 0;
};

// makes the game's files those of the enabled mods (a later one in the list wins a file both have), restoring the
// rest. progress(done, total) may be null
Result Sync(const QString& assets, const QList<Mod>& enabled, const std::function<void(int, int)>& progress);

// empties the game's cache drive (hdd/Z), which keeps copies of audio and textures read from the disc
bool ClearCache(const QString& cacheDrive);

}  // namespace mods
