#include "online.h"

#include <QNetworkDatagram>
#include <QFile>
#include <QHostInfo>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QUdpSocket>

#include <miniupnpc.h>
#include <upnpcommands.h>
#include <upnperrors.h>

#include <array>
#include <cstring>
#include <memory>

namespace online {

namespace {

constexpr int kWords = 6;
constexpr quint64 kVersion = 1;  // 2 bits
const char* kDescription = "Conker Recomped & Reloaded";  // the router's name for the port it opens

const QStringList& Words() {
    static const QStringList words = [] {
        QFile f(":/roomwords.txt");
        f.open(QIODevice::ReadOnly);
        return QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
    }();
    return words;
}

// a 32-bit mask from the key, so the address bits differ with every key (splitmix-style mixing)
quint32 Mask(quint32 key) {
    quint64 z = quint64(key) * 0x9E3779B97F4A7C15ull + 0xC0FFEE;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return quint32(z ^ (z >> 31));
}

quint8 Check(quint64 body) {  // over the version, key and address bits
    quint8 sum = 0xA5;
    for (int i = 0; i < 7; i++) sum = quint8((sum << 3 | sum >> 5) ^ quint8(body >> (8 * i)));
    return sum;
}

}  // namespace

quint32 NewKey() { return 1 + QRandomGenerator::system()->bounded(0xFFFFFu); }

QString Encode(const Code& c) {
    const quint32 ip = c.publicIp.protocol() == QAbstractSocket::IPv4Protocol ? c.publicIp.toIPv4Address() : 0;
    const quint64 body = kVersion << 52 | quint64(c.key & 0xFFFFF) << 32 | quint64(ip ^ Mask(c.key));
    quint64 v = body << 8 | Check(body);  // 62 bits
    const QStringList& words = Words();
    QStringList out;
    for (int i = 0; i < kWords; i++) {
        out.prepend(words[int(v % quint64(words.size()))].toUpper());
        v /= quint64(words.size());
    }
    return out.join('-');
}

Code Decode(const QString& text) {
    Code c;
    const QStringList& words = Words();
    const QStringList given = text.toLower().split(QRegularExpression("[^a-z]+"), Qt::SkipEmptyParts);
    if (given.size() != kWords || words.size() != 1296) return c;
    quint64 v = 0;
    for (const QString& g : given) {
        int at = int(words.indexOf(g));
        if (at < 0 && g.size() >= 3)  // the first three letters name a word, so a typo after them still finds it
            for (int i = 0; i < words.size(); i++)
                if (words[i].startsWith(g.left(3))) { at = i; break; }
        if (at < 0) return c;
        v = v * quint64(words.size()) + quint64(at);
    }
    if (v >> 62) return c;
    const quint64 body = v >> 8;
    if (quint8(v) != Check(body) || body >> 52 != kVersion) return c;
    c.key = quint32(body >> 32) & 0xFFFFF;
    c.publicIp = QHostAddress(quint32(body) ^ Mask(c.key));
    c.valid = !c.publicIp.isNull() && c.publicIp.toIPv4Address() != 0;
    return c;
}

QHostAddress LocalAddress() {
    // the address the default route leaves from: a UDP socket "connected" to a public address sends nothing
    QUdpSocket s;
    s.connectToHost(QHostAddress("192.0.2.1"), 9);  // TEST-NET-1, never reached
    QHostAddress a = s.localAddress();
    return a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback() ? a : QHostAddress();
}

// ── the router ──────────────────────────────────────────────────────────────────────────────────────────────────────

Router::Router(QObject* parent) : QObject(parent) {}

Router::~Router() { Close(); }

void Router::Open(quint16 port) {
    struct Result {
        bool ok = false;
        QString publicIp, detail, controlUrl, serviceType, lan;
    };
    auto r = std::make_shared<Result>();
    QThread* worker = QThread::create([r, port] {
        int error = 0;
        UPNPDev* devices = upnpDiscover(2000, nullptr, nullptr, 0, 0, 2, &error);
        if (!devices) {
            r->detail = QObject::tr("No router answered (UPnP is off, or not supported).");
            return;
        }
        UPNPUrls urls{};
        IGDdatas data{};
        char lan[64] = {}, wan[64] = {};
        const int igd = UPNP_GetValidIGD(devices, &urls, &data, lan, sizeof lan, wan, sizeof wan);
        freeUPNPDevlist(devices);
        if (igd <= 0) {
            r->detail = QObject::tr("No internet gateway found on this network.");
            return;
        }
        char external[40] = {};
        if (UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, external) == UPNPCOMMAND_SUCCESS)
            r->publicIp = QString::fromLatin1(external);
        const QByteArray p = QByteArray::number(port);
        // 0 lease: until removed (some routers refuse timed leases); Close() removes it
        const int add = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, p.constData(), p.constData(), lan,
                                            kDescription, "UDP", nullptr, "0");
        if (add == UPNPCOMMAND_SUCCESS) {
            r->ok = true;
            r->controlUrl = QString::fromLatin1(urls.controlURL);
            r->serviceType = QString::fromLatin1(data.first.servicetype);
            r->lan = QString::fromLatin1(lan);
        } else {
            r->detail = QObject::tr("The router refused to open the port (%1).").arg(QString::fromLatin1(strupnperror(add)));
        }
        // 1: connected; 2: its own internet address is a private one (carrier-grade NAT, or another router in front),
        // so nobody past it can reach this PC whatever is forwarded; 3: not connected
        if (igd == 2)
            r->detail += (r->detail.isEmpty() ? "" : " ") +
                         QObject::tr("Your router's own internet address is a private one (your provider shares one "
                                     "public address between customers, or another router is in front of it): "
                                     "players outside your network can't reach you this way. Use a VPN such as "
                                     "Tailscale or ZeroTier, or ask your provider for a public IPv4 address.");
        else if (igd >= 3)
            r->detail += (r->detail.isEmpty() ? "" : " ") +
                         QObject::tr("The router reports it is not connected to the internet.");
        FreeUPNPUrls(&urls);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QThread::finished, this, [this, r, port] {
        if (r->ok) {
            mapped_ = port;
            controlUrl_ = r->controlUrl;
            serviceType_ = r->serviceType;
            lanAddr_ = r->lan;
        }
        emit Opened(r->ok, QHostAddress(r->publicIp), r->detail);
    });
    worker->start();
}

void Router::Close() {
    if (!mapped_) return;
    const QByteArray p = QByteArray::number(mapped_);
    UPNP_DeletePortMapping(controlUrl_.toLatin1().constData(), serviceType_.toLatin1().constData(), p.constData(),
                           "UDP", nullptr);
    mapped_ = 0;
}

// RFC 5389 binding request: the reply's XOR-MAPPED-ADDRESS is this PC's address as the internet sees it
void Router::AskPublicAddress() {
    auto* sock = new QUdpSocket(this);
    sock->bind(QHostAddress::AnyIPv4, 0);
    QByteArray req(20, 0);
    req[1] = 0x01;                                            // binding request
    const quint32 cookie = 0x2112A442;
    for (int i = 0; i < 4; i++) req[4 + i] = char(cookie >> (24 - 8 * i));
    for (int i = 8; i < 20; i++) req[i] = char(QRandomGenerator::global()->bounded(256));
    const QByteArray id = req.mid(8, 12);
    auto done = std::make_shared<bool>(false);
    auto finish = [this, sock, done](const QHostAddress& ip, const QString& error) {
        if (*done) return;
        *done = true;
        sock->deleteLater();
        emit PublicAddress(ip, error);
    };
    connect(sock, &QUdpSocket::readyRead, this, [sock, id, cookie, finish] {
        while (sock->hasPendingDatagrams()) {
            const QByteArray d = sock->receiveDatagram().data();
            if (d.size() < 20 || d.mid(8, 12) != id) continue;
            for (int at = 20; at + 4 <= d.size();) {
                const int type = quint8(d[at]) << 8 | quint8(d[at + 1]);
                const int len = quint8(d[at + 2]) << 8 | quint8(d[at + 3]);
                if (at + 4 + len > d.size()) break;
                if ((type == 0x0020 || type == 0x0001) && len >= 8 && d[at + 5] == 0x01) {  // (XOR-)MAPPED-ADDRESS, IPv4
                    quint32 ip = 0;
                    for (int i = 0; i < 4; i++) ip = ip << 8 | quint8(d[at + 8 + i]);
                    if (type == 0x0020) ip ^= cookie;
                    finish(QHostAddress(ip), {});
                    return;
                }
                at += 4 + ((len + 3) & ~3);
            }
        }
    });
    QHostInfo::lookupHost("stun.cloudflare.com", this, [sock, req, finish](const QHostInfo& info) {
        QHostAddress server;
        for (const QHostAddress& a : info.addresses())
            if (a.protocol() == QAbstractSocket::IPv4Protocol) server = a;
        if (server.isNull()) {
            finish({}, QObject::tr("Could not reach stun.cloudflare.com (no internet connection?)."));
            return;
        }
        for (int i = 0; i < 3; i++) sock->writeDatagram(req, server, 3478);
    });
    QTimer::singleShot(5000, this, [finish] { finish({}, QObject::tr("No answer from stun.cloudflare.com.")); });
}

}  // namespace online
