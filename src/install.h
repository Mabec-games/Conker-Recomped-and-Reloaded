#pragma once
// The install: one folder holding everything, nothing written anywhere else.
//
// The root is the launcher's folder, or for the AppImage the folder the .AppImage file is in. What ships with the
// launcher (the build kit setup-linux, the Zig compiler, the licence texts) is read from beside the program or, in the
// AppImage, from its usr/share/conker-launcher and usr/share/doc/conker-launcher (read-only, never copied out).
//
//   <root>/ConkerLauncher         this program (or the .AppImage), with launcher.ini (its preferences)
//   <root>/game/Game              built on the first run from the player's disc image
//   <root>/game/portable.txt      keeps the game's settings, saves, logs and caches in game/ (no XDG or %APPDATA%)
//   <root>/game/assets/           the disc's files, extracted on the first run
//   <root>/mods/                  mods, the decensor among them
//   <root>/build/                 while building only: the kit, the compiler and the generated code
#include <QString>

#include <atomic>
#include <functional>

namespace install {

QString Root();        // the launcher's folder (the AppImage's own folder)
QString Shipped(const QString& name);  // a file or folder that ships with the launcher, "" when absent
QString BundledCompiler();  // the Zig shipped with the launcher (its folder, with zig in it), "" when not shipped
QString LicensesDir();      // the licence texts that ship with the launcher
QString IniFile();     // <root>/launcher.ini
QString GameDir();     // <root>/game
QString GameExe();     // <root>/game/Game(.exe)
QString Assets();      // <root>/game/assets
QString ModsDir();     // <root>/mods
QString WorkDir();     // <root>/build
QString SetupTool();   // <root>/setup-linux or setup-windows.exe
bool GameBuilt();

// The compiler the build kit uses: Zig, pinned by the kit (live-and-recompiled tools/toolchains.cmake; keep these in
// step with it). The kit downloads it into build/toolchain/<os>-<host> on its first run and reuses it while its
// sha256.txt stamp matches. Kept after a build, so a rebuild needs no download and works offline.
QString CompilerArchiveName();  // zig-x86_64-linux-0.16.0.tar.xz
QString CompilerUrl();
QString CompilerSha256();
qint64 CompilerDownloadBytes();
QString CompilerDir();          // build/toolchain/linux-linux
bool CompilerReady();           // unpacked there, with the matching stamp
QString OfflineCompiler();      // the archive placed beside the launcher, "" when none
// checks and unpacks the archive into CompilerDir() as the kit would; false with error set
bool UnpackCompiler(const QString& archive, QString* error);
qint64 FolderSize(const QString& dir);
void MakePortable();   // writes game/portable.txt

// the default.xbe SHA-1s the build kit accepts: the USA disc's, and the Microsoft Store copy's (same code)
bool SupportedXbe(const QString& sha1);

struct Disc {
    bool ok = false;
    QString error;      // why not
    QString sha1;       // of its default.xbe
    qint64 bytes = 0;   // all its files
    int files = 0;
};
// reads an Xbox disc image's file table and checks its default.xbe; fast (only the table and the xbe are read)
Disc CheckIso(const QString& iso);

// extracts every file of the image into dir. progress(done, total) in bytes; stop set from another thread cancels.
// false with error set on failure (dir may then hold part of the files)
bool ExtractIso(const QString& iso, const QString& dir, const std::function<void(qint64, qint64)>& progress,
                const std::atomic<bool>& stop, QString* error);

}  // namespace install
