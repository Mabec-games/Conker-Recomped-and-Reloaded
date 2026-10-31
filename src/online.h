#pragma once
// Online play over the game's System Link: one player hosts, the others join with a code. No server of ours is
// involved: the host's router opens the port (UPnP), or the player forwards it by hand.
//
// A room code is 6 words from the EFF short word list 2.0 (1296 words, each with its own first three letters), new each
// time a room is made. It carries 62 bits: a format version, the room's 20-bit key, the host's public IPv4 scrambled
// with the key (one address never looks the same twice) and a checksum. The key is also the game's room key
// (system_link_key): the host's game takes from past its network only datagrams with it, so a code stops working when
// the host makes a new one. The port is not in the code: every player's game uses the same (3074 unless changed).
// It is not encryption: anyone with a code and this launcher can work the address out, and a connected player sees it.
#include <QHostAddress>
#include <QObject>
#include <QString>

namespace online {

struct Code {
    QHostAddress publicIp;
    quint32 key = 0;  // 20 bits; 0: a room without a key (system link as before, open to anyone with the address)
    bool valid = false;
};

QString Encode(const Code& c);     // WORD-WORD-WORD-WORD-WORD-WORD
Code Decode(const QString& text);  // any case and separators; a word may be given by its first three letters
quint32 NewKey();                  // a random, non-zero 20-bit key
QHostAddress LocalAddress();       // this PC's LAN IPv4 (the one its default route leaves from)

// The router: opens and closes the system link port with UPnP (miniupnpc), and tells the public address. All of it
// blocks for a few seconds at most, so it runs on a worker thread; results come back as signals on this object's
// thread.
class Router : public QObject {
    Q_OBJECT
public:
    explicit Router(QObject* parent = nullptr);
    ~Router() override;
    void Open(quint16 port);   // -> Opened
    void Close();              // the mapping this made, if any (blocks briefly; also on destruction)
    // the public address another way, when the router does not speak UPnP: a STUN binding request to a public STUN
    // server (stun.cloudflare.com) -> PublicAddress
    void AskPublicAddress();

signals:
    // ok: the router forwards the port. publicIp may be set even when !ok (the router told it, but refused the port)
    void Opened(bool ok, const QHostAddress& publicIp, const QString& detail);
    void PublicAddress(const QHostAddress& ip, const QString& error);

private:
    quint16 mapped_ = 0;     // the port this mapped, 0 none
    QString controlUrl_, serviceType_, lanAddr_;
};

}  // namespace online
