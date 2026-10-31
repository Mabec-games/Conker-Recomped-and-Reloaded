// Drives the launcher's decensor code without its window:
//   decensor_tool update <modsDir>         latest release from GitHub into <modsDir>/decensor
//   decensor_tool extract <archive> <dir>  only the unpacking, for checking what an archive yields
#include <QCoreApplication>
#include <QTextStream>

#include "../src/decensor.h"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    const QStringList a = app.arguments();
    if (a.size() == 4 && a[1] == "extract") {
        QString error;
        const int n = decensor::Extract(a[2], a[3], &error);
        out << (n < 0 ? "error: " + error : QString("%1 files").arg(n)) << "\n";
        return n < 0;
    }
    if (a.size() != 3 || a[1] != "update") {
        out << "usage: decensor_tool update <modsDir> | extract <archive> <dir>\n";
        return 2;
    }
    Decensor d;
    int last = -1;
    QObject::connect(&d, &Decensor::Checked, [&](bool ok, const QString& error) {
        if (!ok) {
            out << "check failed: " << error << "\n";
            app.exit(1);
            return;
        }
        out << "latest " << d.Latest().version << " sha256 " << d.Latest().sha256 << "\n"
            << "installed '" << Decensor::InstalledVersion(a[2]) << "', update available: "
            << d.UpdateAvailable(a[2]) << "\n";
        out.flush();
        if (!d.UpdateAvailable(a[2])) {
            app.exit(0);
            return;
        }
        d.Install(a[2]);
    });
    QObject::connect(&d, &Decensor::Progress, [&](const QString& stage, qint64 done, qint64 total) {
        const int pct = total > 0 ? int(done * 100 / total) : -1;
        if (pct / 25 != last / 25 || total == 0) out << stage << " " << pct << "%\n", out.flush();
        last = pct;
    });
    QObject::connect(&d, &Decensor::Finished, [&](bool ok, const QString& message) {
        out << (ok ? "ok: " : "failed: ") << message << "\n";
        app.exit(ok ? 0 : 1);
    });
    d.Check();
    return app.exec();
}
