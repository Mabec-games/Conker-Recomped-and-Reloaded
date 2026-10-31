#include "mods.h"

#include <QDir>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace mods {

namespace {

constexpr const char* kState = ".launcher-mods";

QString StateDir(const QString& assets) { return assets + "/" + kState; }
QString Manifest(const QString& assets) { return StateDir(assets) + "/manifest.json"; }
QString Original(const QString& assets, const QString& rel) { return StateDir(assets) + "/original/" + rel; }

// the files under dir, relative, without the launcher's own folder and mod.txt
QStringList Files(const QString& dir) {
    QStringList out;
    QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    const QDir base(dir);
    while (it.hasNext()) {
        const QString rel = base.relativeFilePath(it.next());
        if (rel.startsWith(QString(kState) + "/") || rel.compare("mod.txt", Qt::CaseInsensitive) == 0 ||
            rel.compare("version.json", Qt::CaseInsensitive) == 0)
            continue;
        out << rel;
    }
    return out;
}

// manifest: game file (its real relative path) -> {mod, size, modified} of the copy put there
QJsonObject ReadManifest(const QString& assets) {
    QFile f(Manifest(assets));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool WriteManifest(const QString& assets, const QJsonObject& files) {
    QDir().mkpath(StateDir(assets));
    QSaveFile f(Manifest(assets));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(files).toJson());
    return f.commit();
}

bool CopyOver(const QString& from, const QString& to) {
    QDir().mkpath(QFileInfo(to).absolutePath());
    const QString tmp = to + ".launcher-part";
    QFile::remove(tmp);
    if (!QFile::copy(from, tmp)) return false;
    QFile::remove(to);
    return QFile::rename(tmp, to);
}

bool MoveFile(const QString& from, const QString& to) {
    QDir().mkpath(QFileInfo(to).absolutePath());
    QFile::remove(to);
    if (QFile::rename(from, to)) return true;
    return QFile::copy(from, to) && QFile::remove(from);
}

QJsonObject Stamp(const QString& mod, const QString& source) {
    const QFileInfo fi(source);
    return {{"mod", mod}, {"size", double(fi.size())}, {"modified", fi.lastModified().toString(Qt::ISODateWithMs)}};
}

}  // namespace

QList<Mod> List(const QString& modsDir) {
    QList<Mod> out;
    if (modsDir.isEmpty()) return out;
    for (const QFileInfo& d : QDir(modsDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        Mod m;
        m.name = d.fileName();
        m.path = d.absoluteFilePath();
        QFile txt(m.path + "/mod.txt");
        if (txt.open(QIODevice::ReadOnly)) m.description = QString::fromUtf8(txt.readAll()).trimmed();
        m.files = int(Files(m.path).size());
        if (m.files) out << m;
    }
    return out;
}

QStringList Applied(const QString& assets) {
    QStringList out;
    const QJsonObject files = ReadManifest(assets);
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QString mod = it.value().toObject().value("mod").toString();
        if (!out.contains(mod)) out << mod;
    }
    return out;
}

Result Sync(const QString& assets, const QList<Mod>& enabled, const std::function<void(int, int)>& progress) {
    Result r;
    if (!QFileInfo::exists(assets + "/default.xbe")) {
        r.ok = false;
        r.error = QObject::tr("Mods need the game's extracted files in %1 (default.xbe with its folders), not only a "
                              "disc image.").arg(assets);
        return r;
    }

    // the game's files by lower case path: the game ignores case, and so do the mods' paths
    QHash<QString, QString> real;
    for (const QString& rel : Files(assets)) real.insert(rel.toLower(), rel);

    // what each game file should hold: the last enabled mod with it wins
    struct Want {
        QString mod, source;
    };
    QHash<QString, Want> want;
    for (const Mod& m : enabled)
        for (const QString& rel : Files(m.path)) {
            auto it = real.find(rel.toLower());
            if (it == real.end()) continue;  // only files the game has: a mod replaces, it does not add
            want.insert(it.value(), {m.name, m.path + "/" + rel});
        }

    QJsonObject manifest = ReadManifest(assets);
    // files a mod no longer wants: their originals back
    QStringList restore;
    for (auto it = manifest.begin(); it != manifest.end(); ++it)
        if (!want.contains(it.key())) restore << it.key();
    // files to (re)write: new, from another mod, or the mod's copy changed
    QStringList write;
    for (auto it = want.begin(); it != want.end(); ++it) {
        const QJsonObject have = manifest.value(it.key()).toObject();
        if (have != Stamp(it.value().mod, it.value().source)) write << it.key();
    }

    const int total = int(restore.size() + write.size());
    int done = 0;
    auto step = [&] {
        if (progress) progress(++done, total);
    };
    auto fail = [&](const QString& what) {
        r.ok = false;
        r.error = what;
        WriteManifest(assets, manifest);  // what was done so far stays known
        return r;
    };

    for (const QString& rel : restore) {
        const QString original = Original(assets, rel);
        if (QFileInfo::exists(original) && !MoveFile(original, assets + "/" + rel))
            return fail(QObject::tr("Could not restore %1").arg(rel));
        manifest.remove(rel);
        r.restored++;
        step();
    }
    for (const QString& rel : write) {
        const Want& w = want[rel];
        // the first time a mod takes this file, the game's own is moved aside
        if (!manifest.contains(rel) && !MoveFile(assets + "/" + rel, Original(assets, rel)))
            return fail(QObject::tr("Could not set aside the original %1").arg(rel));
        if (!CopyOver(w.source, assets + "/" + rel)) return fail(QObject::tr("Could not copy %1").arg(w.source));
        manifest.insert(rel, Stamp(w.mod, w.source));
        r.replaced++;
        step();
    }
    if (!WriteManifest(assets, manifest)) return fail(QObject::tr("Could not write %1").arg(Manifest(assets)));
    r.changed = total > 0;
    if (manifest.isEmpty()) QDir(StateDir(assets)).removeRecursively();
    return r;
}

bool ClearCache(const QString& cacheDrive) {
    QDir dir(cacheDrive);
    if (!dir.exists()) return true;
    bool ok = true;
    for (const QFileInfo& e : dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
        if (e.isDir() && !e.isSymLink()) ok &= QDir(e.absoluteFilePath()).removeRecursively();
        else ok &= QFile::remove(e.absoluteFilePath());
    }
    return ok;
}

}  // namespace mods
