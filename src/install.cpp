#include "install.h"

#include <algorithm>
#include <QCoreApplication>
#include <QDirIterator>

#include <archive.h>
#include <archive_entry.h>
#include <memory>

#include <cstring>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

#include <vector>

namespace install {

QString Root() {
    // an AppImage runs from a read-only mount: its install is the folder the .AppImage file sits in
    const QString appimage = qEnvironmentVariable("APPIMAGE");
    if (!appimage.isEmpty()) return QFileInfo(appimage).absolutePath();
    return QCoreApplication::applicationDirPath();
}

QString Shipped(const QString& name) {
    const QString bin = QCoreApplication::applicationDirPath();
    const QString appdir = qEnvironmentVariable("APPDIR");  // the AppImage's mount, whatever path the program was given
    for (const QString& p : {bin + "/" + name, appdir.isEmpty() ? QString() : appdir + "/usr/share/conker-launcher/" + name,
                             appdir.isEmpty() ? QString() : appdir + "/usr/share/doc/conker-launcher/" + name, bin + "/../share/conker-launcher/" + name, bin + "/../share/doc/conker-launcher/" + name})
        if (!p.isEmpty() && QFileInfo::exists(p)) return QDir::cleanPath(p);
    return {};
}

QString BundledCompiler() {
    const QString dir = Shipped("zig");
#ifdef Q_OS_WIN
    const QString zig = dir + "/zig.exe";
#else
    const QString zig = dir + "/zig";
#endif
    return !dir.isEmpty() && QFileInfo(zig).isExecutable() ? dir : QString();
}

QString LicensesDir() { return Shipped("licenses"); }
QString IniFile() { return Root() + "/launcher.ini"; }
QString GameDir() { return Root() + "/game"; }
QString Assets() { return GameDir() + "/assets"; }
QString ModsDir() { return Root() + "/mods"; }
QString WorkDir() { return Root() + "/build"; }

QString GameExe() {
#ifdef Q_OS_WIN
    return GameDir() + "/Game.exe";
#else
    return GameDir() + "/Game";
#endif
}

QString SetupTool() {
#ifdef Q_OS_WIN
    const QString name = "setup-windows.exe";
#else
    const QString name = "setup-linux";
#endif
    const QString shipped = Shipped(name);
    return shipped.isEmpty() ? Root() + "/" + name : shipped;
}

bool GameBuilt() { return QFileInfo(GameExe()).isFile(); }

QString KitId() {
    static QString id = [] {
        QFile f(SetupTool());
        if (!f.open(QIODevice::ReadOnly)) return QString();
        QCryptographicHash h(QCryptographicHash::Sha256);
        h.addData(&f);
        return QString::fromLatin1(h.result().toHex());
    }();
    return id;
}

void MarkKitBuilt() {
    if (!KitId().isEmpty()) QSettings(IniFile(), QSettings::IniFormat).setValue("game/kit", KitId());
}

bool KitChanged() {
    return GameBuilt() && !KitId().isEmpty() &&
           QSettings(IniFile(), QSettings::IniFormat).value("game/kit").toString() != KitId();
}

QString CompilerArchiveName() {
#ifdef Q_OS_WIN
    return "zig-x86_64-windows-0.16.0.zip";
#else
    return "zig-x86_64-linux-0.16.0.tar.xz";
#endif
}
QString CompilerUrl() { return "https://ziglang.org/download/0.16.0/" + CompilerArchiveName(); }
QString CompilerSha256() {
#ifdef Q_OS_WIN
    return "68659eb5f1e4eb1437a722f1dd889c5a322c9954607f5edcf337bc3684a75a7e";
#else
    return "70e49664a74374b48b51e6f3fdfbf437f6395d42509050588bd49abe52ba3d00";
#endif
}
qint64 CompilerDownloadBytes() {
#ifdef Q_OS_WIN
    return 97217739;
#else
    return 55478392;
#endif
}
QString CompilerDir() {
#ifdef Q_OS_WIN
    return WorkDir() + "/toolchain/windows-windows";
#else
    return WorkDir() + "/toolchain/linux-linux";
#endif
}
bool CompilerReady() {
    QFile stamp(CompilerDir() + "/sha256.txt");
    return stamp.open(QIODevice::ReadOnly) && QString::fromLatin1(stamp.readAll()).trimmed() == CompilerSha256();
}
QString OfflineCompiler() {
    const QString p = Root() + "/" + CompilerArchiveName();
    return QFileInfo(p).isFile() ? p : QString();
}

qint64 FolderSize(const QString& dir) {
    qint64 n = 0;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) n += it.nextFileInfo().size();
    return n;
}

bool UnpackCompiler(const QString& archivePath, QString* error) {
    // the file the kit would download, byte for byte
    {
        QFile f(archivePath);
        QCryptographicHash sha(QCryptographicHash::Sha256);
        if (!f.open(QIODevice::ReadOnly) || !sha.addData(&f)) {
            *error = QObject::tr("Could not read %1").arg(archivePath);
            return false;
        }
        if (QString::fromLatin1(sha.result().toHex()) != CompilerSha256()) {
            *error = QObject::tr("%1 is not the expected compiler (its checksum differs). Download it again from %2")
                         .arg(QFileInfo(archivePath).fileName(), CompilerUrl());
            return false;
        }
    }
    const QString fresh = CompilerDir() + ".new";
    QDir(fresh).removeRecursively();
    QDir().mkpath(fresh);
    struct archive* a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
    std::unique_ptr<struct archive, int (*)(struct archive*)> guard(a, archive_read_free);
#ifdef Q_OS_WIN
    const int opened = archive_read_open_filename_w(a, reinterpret_cast<const wchar_t*>(archivePath.utf16()), 1 << 16);
#else
    const int opened = archive_read_open_filename(a, QFile::encodeName(archivePath).constData(), 1 << 16);
#endif
    if (opened != ARCHIVE_OK) {
        *error = QObject::tr("Could not open %1").arg(archivePath);
        return false;
    }
    archive_entry* e;
    int r;
    while ((r = archive_read_next_header(a, &e)) == ARCHIVE_OK) {
        // its single top folder (zig-x86_64-linux-0.16.0/) goes, as the kit's tar --strip-components=1 does
        QString path = QString::fromUtf8(archive_entry_pathname_utf8(e) ? archive_entry_pathname_utf8(e)
                                                                          : archive_entry_pathname(e));
        path.replace('\\', '/');
        const qsizetype slash = path.indexOf('/');
        if (slash < 0) continue;
        path = path.mid(slash + 1);
        if (path.isEmpty() || path.startsWith('/') || path.split('/').contains("..")) continue;
        const QString to = fresh + "/" + path;
        const auto type = archive_entry_filetype(e);
        if (type == AE_IFDIR) {
            QDir().mkpath(to);
            continue;
        }
        QDir().mkpath(QFileInfo(to).absolutePath());
        if (type == AE_IFLNK) {
            QFile::link(QString::fromUtf8(archive_entry_symlink_utf8(e)), to);
            continue;
        }
        if (type != AE_IFREG) continue;
        QFile out(to);
        if (!out.open(QIODevice::WriteOnly)) {
            *error = QObject::tr("Could not write %1").arg(to);
            return false;
        }
        const void* block;
        size_t size;
        la_int64_t offset;
        while (archive_read_data_block(a, &block, &size, &offset) == ARCHIVE_OK)
            out.write(static_cast<const char*>(block), qint64(size));
        out.close();
        if (archive_entry_perm(e) & 0111)
            out.setPermissions(out.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
    }
    if (r != ARCHIVE_EOF) {
        *error = QObject::tr("%1 is damaged.").arg(archivePath);
        return false;
    }
    QFile stamp(fresh + "/sha256.txt");
    if (!stamp.open(QIODevice::WriteOnly) || stamp.write(CompilerSha256().toLatin1()) != 64) {
        *error = QObject::tr("Could not write the compiler's stamp.");
        return false;
    }
    stamp.close();
    QDir(CompilerDir()).removeRecursively();
    if (!QDir().rename(fresh, CompilerDir())) {
        *error = QObject::tr("Could not move the compiler into %1").arg(CompilerDir());
        return false;
    }
    return true;
}

void MakePortable() {
    QDir().mkpath(GameDir());
    QFile f(GameDir() + "/portable.txt");
    if (f.exists()) return;
    if (f.open(QIODevice::WriteOnly))
        f.write("This file keeps the game's settings, saves, logs and caches in this folder.\n");
}

bool SupportedXbe(const QString& sha1) {
    return sha1 == "2232a1b3874d67b4bb4cf53a940420d2dab59535" ||  // USA disc
           sha1 == "68a37f0d03d73dabc86a848df5ec1820dd551254";    // Microsoft Store (same code, another header)
}

// ── Xbox DVD file system (XDVDFS) ────────────────────────────────────────────────────────────────────────────────────
// A volume descriptor at sector 32 of the game partition ("MICROSOFT*XBOX*MEDIA", then the root directory's sector
// and size). A directory is a binary tree of entries: left and right subtree offsets in 4-byte units, the entry's
// first sector and size, attributes (0x10 a directory) and its name.

namespace {

constexpr qint64 kSector = 2048;
const char kMagic[] = "MICROSOFT*XBOX*MEDIA";
// where the game partition starts: a plain image, or a full dump after the video partition (Redump, XGD2, XGD3)
constexpr qint64 kPartitions[] = {0, 0x18300000, 0x2080000, 0xFD90000};

struct Entry {
    QString path;  // relative, '/' separated
    qint64 sector = 0, size = 0;
};

struct Volume {
    QFile file;
    qint64 base = -1;
    std::vector<Entry> files;
    QString error;

    bool Read(qint64 at, char* to, qint64 n) { return file.seek(at) && file.read(to, n) == n; }

    // a name the host can take as one path component
    static bool SafeName(const QString& n) {
        return !n.isEmpty() && n != "." && n != ".." && !n.contains('/') && !n.contains('\\') && !n.contains(':') &&
               !n.contains(QChar(0));
    }

    bool Walk(qint64 sector, qint64 size, const QString& prefix, int depth) {
        if (depth > 32 || size <= 0 || size > (qint64(16) << 20)) {
            error = QObject::tr("The disc's folder table is damaged.");
            return false;
        }
        QByteArray dir(size, 0);
        if (!Read(base + sector * kSector, dir.data(), size)) {
            error = QObject::tr("Could not read the disc image.");
            return false;
        }
        std::vector<qint64> stack{0};
        std::vector<bool> seen(size_t(size / 4) + 1);
        while (!stack.empty()) {
            const qint64 o = stack.back();
            stack.pop_back();
            if (o + 14 > size || seen[size_t(o / 4)]) continue;
            seen[size_t(o / 4)] = true;
            const auto* d = reinterpret_cast<const uchar*>(dir.constData() + o);
            const quint16 left = quint16(d[0] | d[1] << 8), right = quint16(d[2] | d[3] << 8);
            if (left == 0xFFFF) continue;  // padding to the sector's end
            const qint64 start = qint64(d[4] | d[5] << 8 | d[6] << 16 | quint32(d[7]) << 24);
            const qint64 bytes = qint64(d[8] | d[9] << 8 | d[10] << 16 | quint32(d[11]) << 24);
            const uchar attr = d[12], len = d[13];
            if (o + 14 + len > size) continue;
            const QString name = QString::fromLatin1(dir.constData() + o + 14, len);
            if (left) stack.push_back(qint64(left) * 4);
            if (right) stack.push_back(qint64(right) * 4);
            if (!SafeName(name)) {
                error = QObject::tr("The disc has a file name that is not safe to write: %1").arg(name);
                return false;
            }
            const QString path = prefix.isEmpty() ? name : prefix + "/" + name;
            if (attr & 0x10) {
                if (bytes && !Walk(start, bytes, path, depth + 1)) return false;
            } else {
                files.push_back({path, start, bytes});
            }
        }
        return true;
    }

    bool Open(const QString& iso) {
        file.setFileName(iso);
        if (!file.open(QIODevice::ReadOnly)) {
            error = QObject::tr("Could not open %1").arg(iso);
            return false;
        }
        char head[28];
        for (qint64 p : kPartitions)
            if (Read(p + 32 * kSector, head, sizeof head) && std::memcmp(head, kMagic, 20) == 0) {
                base = p;
                break;
            }
        if (base < 0) {
            error = QObject::tr("This is not an original Xbox disc image.");
            return false;
        }
        const auto* h = reinterpret_cast<const uchar*>(head);
        const qint64 root = qint64(h[20] | h[21] << 8 | h[22] << 16 | quint32(h[23]) << 24);
        const qint64 rootSize = qint64(h[24] | h[25] << 8 | h[26] << 16 | quint32(h[27]) << 24);
        return Walk(root, rootSize, QString(), 0);
    }
};

}  // namespace

Disc CheckIso(const QString& iso) {
    Disc d;
    Volume v;
    if (!v.Open(iso)) {
        d.error = v.error;
        return d;
    }
    const Entry* xbe = nullptr;
    for (const Entry& e : v.files) {
        d.bytes += e.size;
        d.files++;
        if (e.path.compare("default.xbe", Qt::CaseInsensitive) == 0) xbe = &e;
    }
    if (!xbe || xbe->size > (qint64(64) << 20)) {
        d.error = QObject::tr("The disc image has no default.xbe: it is not an Xbox game disc.");
        return d;
    }
    QByteArray data(xbe->size, 0);
    if (!v.Read(v.base + xbe->sector * kSector, data.data(), xbe->size)) {
        d.error = QObject::tr("Could not read the disc image.");
        return d;
    }
    d.sha1 = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
    if (!SupportedXbe(d.sha1)) {
        d.error = QObject::tr("This is a different release of the game than the one supported. Conker: Live & "
                              "Reloaded for the original Xbox, USA disc, is needed (its default.xbe has SHA-1 "
                              "2232a1b3…, this one has %1).").arg(d.sha1);
        return d;
    }
    d.ok = true;
    return d;
}

bool ExtractIso(const QString& iso, const QString& dir, const std::function<void(qint64, qint64)>& progress,
                const std::atomic<bool>& stop, QString* error) {
    Volume v;
    if (!v.Open(iso)) {
        *error = v.error;
        return false;
    }
    qint64 total = 0, done = 0;
    for (const Entry& e : v.files) total += e.size;
    QByteArray buf(qint64(4) << 20, 0);
    for (const Entry& e : v.files) {
        const QString to = dir + "/" + e.path;
        QDir().mkpath(QFileInfo(to).absolutePath());
        QFile out(to);
        if (!out.open(QIODevice::WriteOnly)) {
            *error = QObject::tr("Could not write %1").arg(to);
            return false;
        }
        if (!v.file.seek(v.base + e.sector * kSector)) {
            *error = QObject::tr("Could not read the disc image.");
            return false;
        }
        for (qint64 left = e.size; left > 0;) {
            if (stop) {
                *error = QObject::tr("Cancelled.");
                return false;
            }
            const qint64 n = std::min<qint64>(left, buf.size());
            if (v.file.read(buf.data(), n) != n) {
                *error = QObject::tr("Could not read the disc image (is it complete?).");
                return false;
            }
            if (out.write(buf.constData(), n) != n) {
                *error = QObject::tr("Could not write %1 (disk full?)").arg(to);
                return false;
            }
            left -= n;
            done += n;
            if (progress) progress(done, total);
        }
    }
    return true;
}

}  // namespace install
