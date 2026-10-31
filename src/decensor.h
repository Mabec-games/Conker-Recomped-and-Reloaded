#pragma once
// The decensor mod (github.com/nastys/conker-live-and-reloaded-decensor), kept up to date from its releases:
// the latest release's .7z is downloaded, checked against the SHA-256 GitHub lists for it, and unpacked into
// <mods>/decensor. Only files under dvddata/ are taken (the release's scripts are left out and never run), and only
// as plain files inside that folder. The mod is then applied like any other (mods.h), so turning it off restores the
// game's own files.
#include <QObject>
#include <QPointer>
#include <QString>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QCryptographicHash;

class Decensor : public QObject {
    Q_OBJECT
public:
    static constexpr const char* kRepo = "nastys/conker-live-and-reloaded-decensor";
    static constexpr const char* kModName = "decensor";

    struct Release {
        QString version;     // the release's tag, "0.37"
        QString published;   // ISO date
        QString url;         // the .7z asset
        QString sha256;      // from GitHub's asset digest, "" when it lists none
        qint64 size = 0;
        bool valid() const { return !url.isEmpty(); }
    };

    explicit Decensor(QObject* parent = nullptr);

    // what <modsDir>/decensor holds: its version.json's version, "" when absent (or not installed at all)
    static QString InstalledVersion(const QString& modsDir);
    static bool Installed(const QString& modsDir);
    // a newer release than installed: a different tag, or any release over a copy of unknown version
    bool UpdateAvailable(const QString& modsDir) const;

    const Release& Latest() const { return latest_; }
    bool Busy() const { return busy_; }               // checking or installing
    bool Installing() const { return installing_; }   // downloading or unpacking: the mod folder is changing

    void Check();                           // the latest release, from the GitHub API
    void Install(const QString& modsDir);   // downloads and unpacks Latest() (Check first)
    void Cancel();

signals:
    void Checked(bool ok, const QString& error);
    void Progress(const QString& stage, qint64 done, qint64 total);  // total 0: unknown
    void Finished(bool ok, const QString& message);

private:
    void Downloaded();
    void Fail(const QString& error);

    QNetworkAccessManager* net_;
    Release latest_;
    bool busy_ = false;
    bool installing_ = false;
    QString modsDir_;
    QString partPath_;
    QFile* part_ = nullptr;
    QCryptographicHash* hash_ = nullptr;
    QPointer<QNetworkReply> reply_;
};

namespace decensor {
// unpacks the release archive's dvddata/ into dir (made, empty); the file count, or -1 with error set
int Extract(const QString& archive, const QString& dir, QString* error);
}  // namespace decensor
