#include <QApplication>
#include <QFileInfo>
#include <QTabWidget>
#include <QTimer>

#include <QSettings>

#include "gamepad.h"
#include "install.h"
#include "mainwindow.h"

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <filesystem>
#endif

namespace {

// install::Root() before there is a QApplication to ask: the AppImage's folder, or the program's own
QString EarlyRoot() {
    const QString appimage = qEnvironmentVariable("APPIMAGE");
    if (!appimage.isEmpty()) return QFileInfo(appimage).absolutePath();
#ifdef Q_OS_WIN
    wchar_t path[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, path, DWORD(std::size(path)));
    return QFileInfo(QString::fromWCharArray(path, int(n))).absolutePath();
#else
    std::error_code ec;
    const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? QString() : QFileInfo(QString::fromStdString(exe.string())).absolutePath();
#endif
}

// the launcher's size (Launcher tab): Qt reads it once, as the application starts. QT_SCALE_FACTOR set by the player
// wins; "auto" is 125% in a Steam Deck's Game Mode (gamescope: a 7" screen, fingers and a pad) and 100% elsewhere,
// where Qt follows the screen's DPI by itself
void ApplyUiScale() {
    if (qEnvironmentVariableIsSet("QT_SCALE_FACTOR")) return;
    const QString root = EarlyRoot();
    const QString setting = root.isEmpty() ? QString("auto")
                                           : QSettings(root + "/launcher.ini", QSettings::IniFormat)
                                                 .value("launcher/scale", "auto").toString();
    int percent = setting.toInt();
    if (percent < 50 || percent > 300) percent = install::GamescopeSession() ? 125 : 100;
    if (percent != 100) qputenv("QT_SCALE_FACTOR", QByteArray::number(percent / 100.0));
}

}  // namespace

int main(int argc, char** argv) {
    ApplyUiScale();
    QApplication app(argc, argv);
    // preferences go to launcher.ini beside the launcher (install::IniFile), nothing in ~/.config or the registry
    QApplication::setApplicationName("ConkerLauncher");
    QApplication::setDesktopFileName("conker-recomped-and-reloaded");
    MainWindow w;
    Gamepad gamepad;  // controller navigation, for a Steam Deck in Game Mode
    w.show();
    // --play: straight into the game, as the Play button (for a desktop shortcut)
    if (QApplication::arguments().contains("--play")) QTimer::singleShot(0, &w, &MainWindow::Play);
    // --install ISO: setup from that disc image straight away (for scripted installs and testing)
    if (const qsizetype i = QApplication::arguments().indexOf("--install"); i >= 0 && i + 1 < QApplication::arguments().size()) {
        const QString iso = QApplication::arguments()[i + 1];
        QTimer::singleShot(0, &w, [&w, iso] { w.InstallFrom(iso); });
    }
    // --screenshot FILE [TAB]: renders the window to FILE and quits (for checking the layout headlessly)
    const QStringList args = QApplication::arguments();
    if (const qsizetype i = args.indexOf("--screenshot"); i >= 0 && i + 1 < args.size()) {
        const QString file = args[i + 1];
        const int tab = i + 2 < args.size() ? args[i + 2].toInt() : 0;
        QTimer::singleShot(300, &w, [&w, file, tab] {
            if (auto* tabs = w.findChild<QTabWidget*>()) tabs->setCurrentIndex(tab);
            QTimer::singleShot(200, &w, [&w, file] {
                w.grab().save(file);
                QApplication::quit();
            });
        });
    }
    return app.exec();
}
