// Checks the online pieces without the launcher's window:
//   online_tool codes   join codes round trip, typos tolerated, damage caught
//   online_tool probe   the router: UPnP discovery and the public address it reports (read only: opens nothing)
//   online_tool stun    the public address from stun.cloudflare.com
#include <QCoreApplication>
#include <QFile>
#include <QRandomGenerator>
#include <QSet>
#include <QTextStream>

#include <miniupnpc.h>
#include <upnpcommands.h>

#include "../src/online.h"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    const QString what = argc > 1 ? argv[1] : "";
    if (what == "codes") {
        QRandomGenerator rng(1234);
        int bad = 0;
        for (int i = 0; i < 10000; i++) {  // random keys and addresses survive the trip
            online::Code c;
            c.publicIp = QHostAddress(rng.generate() | 1);
            c.key = i % 7 == 0 ? 0 : online::NewKey();
            c.valid = true;
            const online::Code back = online::Decode(online::Encode(c));
            bad += !(back.valid && back.publicIp == c.publicIp && back.key == c.key);
        }
        out << "10000 random codes round trip: " << (bad ? QString("%1 FAILED").arg(bad) : QString("ok")) << "\n";
        online::Code c;
        c.publicIp = QHostAddress("203.0.113.77");
        c.valid = true;
        QSet<QString> seen;
        for (int i = 0; i < 50; i++) {
            c.key = online::NewKey();
            seen.insert(online::Encode(c));
        }
        out << "one address, 50 new keys, 50 different codes: " << (seen.size() == 50 ? "ok" : "FAILED") << "\n";
        c.key = online::NewKey();
        const QString code = online::Encode(c);
        out << "example: " << code << "\n";
        QStringList w = code.toLower().split('-');
        QString typed;
        for (QString& x : w) typed += (x.size() > 3 ? x.left(3) + "zz" : x) + "  ";  // first three letters right
        const online::Code t = online::Decode(typed);
        const bool typoOk = t.valid && t.key == c.key && t.publicIp == c.publicIp;
        out << "lowercase, spaces, typos after the first 3 letters: " << (typoOk ? "ok" : "FAILED") << "\n";
        int caught = 0, total = 0;
        const QStringList all = [] { QFile f(":/roomwords.txt"); f.open(QIODevice::ReadOnly); return QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts); }();
        for (int pos = 0; pos < 6; pos++)
            for (int j = 0; j < 200; j++) {
                QStringList ww = code.split('-');
                const QString other = all[int(rng.bounded(all.size()))].toUpper();
                if (other == ww[pos]) continue;
                ww[pos] = other;
                total++;
                caught += !online::Decode(ww.join('-')).valid;
            }
        out << QString("one wrong word caught: %1 of %2 (%3%)\n").arg(caught).arg(total).arg(100.0 * caught / total, 0, 'f', 1);
        out << "not a code: " << (!online::Decode("hello world").valid && !online::Decode("").valid ? "ok" : "FAILED") << "\n";
        return bad || seen.size() != 50 || !typoOk ? 1 : 0;
    }
    if (what == "probe") {
        int error = 0;
        UPNPDev* devices = upnpDiscover(2000, nullptr, nullptr, 0, 0, 2, &error);
        if (!devices) {
            out << "no UPnP router answered (error " << error << ")\n";
            return 1;
        }
        UPNPUrls urls{};
        IGDdatas data{};
        char lan[64] = {}, wan[64] = {}, external[40] = {};
        const int igd = UPNP_GetValidIGD(devices, &urls, &data, lan, sizeof lan, wan, sizeof wan);
        freeUPNPDevlist(devices);
        out << "gateway status " << igd << " (1 connected, 2 private WAN address, 3 not connected), this PC " << lan
            << "\n";
        if (igd > 0 && UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, external) == UPNPCOMMAND_SUCCESS)
            out << "router's public address " << external << "\n";
        if (igd > 0) FreeUPNPUrls(&urls);
        return igd == 1 ? 0 : 1;
    }
    if (what == "stun") {
        online::Router r;
        QObject::connect(&r, &online::Router::PublicAddress, [&](const QHostAddress& ip, const QString& err) {
            out << (ip.isNull() ? "failed: " + err : "public address " + ip.toString()) << "\n";
            app.exit(ip.isNull());
        });
        r.AskPublicAddress();
        return app.exec();
    }
    out << "usage: online_tool codes | probe | stun\n";
    return 2;
}
