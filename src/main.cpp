#include <QApplication>
#include <QTabWidget>
#include <QTimer>

#include <QSettings>

#include "install.h"
#include "mainwindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // preferences go to launcher.ini beside the launcher (install::IniFile), nothing in ~/.config or the registry
    QApplication::setApplicationName("ConkerLauncher");
    QApplication::setDesktopFileName("conker-recomped-and-reloaded");
    MainWindow w;
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
