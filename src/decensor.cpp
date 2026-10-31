#include "decensor.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QThread>

#include <archive.h>
#include <archive_entry.h>

#include <atomic>
#include <memory>

namespace {

constexpr qint64 kMaxUnpacked = qint64(4) << 30;  // no release comes near; a bad archive stops here
const QByteArray kUserAgent = "ConkerLauncher";

QString VersionFile(const QString& modDir) { return modDir + "/version.json"; }

// the part of an archive path from its dvddata folder on, checked to stay inside it: "" when it has none or tries
// to leave (absolute, "..", drive letters)
QString DvddataPath(QString path) {
    path.replace('\\', '/');
    if (path.startsWith('/') || path.contains(':')) return {};
    const QStringList parts = path.split('/', Qt::SkipEmptyParts);
    qsizetype from = -1;
    for (qsizetype i = 0; i < parts.size(); i++) {
        if (parts[i] == ".." ) return {};
        if (from < 0 && parts[i].compare("dvddata", Qt::CaseInsensitive) == 0) from = i;
    }
    if (from < 0 || from + 1 >= parts.size()) return {};
    QStringList kept = parts.mid(from);
    kept.removeAll(".");
    kept[0] = "dvddata";
    return kept.join('/');
}

// a plain name inside a folder (a nested zip's entries): no folders, no tricks
bool PlainName(const QString& name) {
    return !name.isEmpty() && !name.contains('/') && !name.contains('\\') && name != "." && name != ".." &&
           !name.contains(':');
}

struct ArchiveDeleter {
    void operator()(archive* a) const { archive_read_free(a); }
};
using Archive = std::unique_ptr<archive, ArchiveDeleter>;

Archive Open(const QString& path, QString* error) {
    Archive a(archive_read_new());
    archive_read_support_format_all(a.get());
    archive_read_support_filter_all(a.get());
#ifdef Q_OS_WIN
    const int r = archive_read_open_filename_w(a.get(), reinterpret_cast<const wchar_t*>(path.utf16()), 1 << 16);
#else
    const int r = archive_read_open_filename(a.get(), QFile::encodeName(path).constData(), 1 << 16);
#endif
    if (r != ARCHIVE_OK) {
        *error = QObject::tr("Could not open %1: %2").arg(QFileInfo(path).fileName(), archive_error_string(a.get()));
        return nullptr;
    }
    return a;
}

QString EntryName(archive_entry* e) {
    if (const char* utf8 = archive_entry_pathname_utf8(e)) return QString::fromUtf8(utf8);
    if (const char* raw = archive_entry_pathname(e)) return QString::fromLocal8Bit(raw);
    return {};
}

bool WriteEntry(archive* a, const QString& to, qint64* total, QString* error) {
    QDir().mkpath(QFileInfo(to).absolutePath());
    QFile out(to);
    if (!out.open(QIODevice::WriteOnly)) {
        *error = QObject::tr("Could not write %1").arg(to);
        return false;
    }
    const void* block;
    size_t size;
    la_int64_t offset;
    for (;;) {
        const int r = archive_read_data_block(a, &block, &size, &offset);
        if (r == ARCHIVE_EOF) return true;
        if (r != ARCHIVE_OK) {
            *error = QObject::tr("The archive is damaged: %1").arg(archive_error_string(a));
            return false;
        }
        *total += qint64(size);
        if (*total > kMaxUnpacked) {
            *error = QObject::tr("The archive unpacks to more than expected; stopped.");
            return false;
        }
        if (out.write(static_cast<const char*>(block), qint64(size)) != qint64(size)) {
            *error = QObject::tr("Could not write %1 (disk full?)").arg(to);
            return false;
        }
    }
}

// a .zip inside the release (voice banks were once shipped zipped): its files beside it, then it goes
bool ExpandZip(const QString& zip, qint64* total, int* files, QString* error) {
    Archive a = Open(zip, error);
    if (!a) return false;
    const QString dir = QFileInfo(zip).absolutePath();
    archive_entry* e;
    while (archive_read_next_header(a.get(), &e) == ARCHIVE_OK) {
        if (archive_entry_filetype(e) != AE_IFREG) continue;
        const QString name = EntryName(e);
        if (!PlainName(name)) continue;
        if (!WriteEntry(a.get(), dir + "/" + name, total, error)) return false;
        (*files)++;
    }
    QFile::remove(zip);
    return true;
}

}  // namespace

namespace decensor {

int Extract(const QString& archivePath, const QString& dir, QString* error) {
    Archive a = Open(archivePath, error);
    if (!a) return -1;
    QDir().mkpath(dir);
    qint64 total = 0;
    int files = 0;
    archive_entry* e;
    int r;
    while ((r = archive_read_next_header(a.get(), &e)) == ARCHIVE_OK) {
        if (archive_entry_filetype(e) != AE_IFREG) continue;  // folders come with their files; links are left out
        const QString rel = DvddataPath(EntryName(e));
        if (rel.isEmpty()) continue;  // outside dvddata/: the release's scripts and notes
        const QString to = dir + "/" + rel;
        if (!WriteEntry(a.get(), to, &total, error)) return -1;
        if (rel.endsWith(".zip", Qt::CaseInsensitive)) {
            if (!ExpandZip(to, &total, &files, error)) return -1;
        } else {
            files++;
        }
    }
    if (r != ARCHIVE_EOF) {
        *error = QObject::tr("The archive is damaged: %1").arg(archive_error_string(a.get()));
        return -1;
    }
    if (files == 0) {
        *error = QObject::tr("The release has no game files (dvddata/). Its layout may have changed.");
        return -1;
    }
    return files;
}

}  // namespace decensor

Decensor::Decensor(QObject* parent) : QObject(parent), net_(new QNetworkAccessManager(this)) {}

QString Decensor::InstalledVersion(const QString& modsDir) {
    QFile f(VersionFile(modsDir + "/" + kModName));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object().value("version").toString();
}

bool Decensor::Installed(const QString& modsDir) {
    return !modsDir.isEmpty() && QFileInfo(modsDir + "/" + kModName + "/dvddata").isDir();
}

bool Decensor::UpdateAvailable(const QString& modsDir) const {
    if (!latest_.valid()) return false;
    if (!Installed(modsDir)) return true;
    return InstalledVersion(modsDir) != latest_.version;
}

void Decensor::Check() {
    if (busy_) return;
    busy_ = true;
    QNetworkRequest req(QUrl(QString("https://api.github.com/repos/%1/releases/latest").arg(kRepo)));
    req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(15000);
    QNetworkReply* reply = net_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        busy_ = false;
        if (reply->error() != QNetworkReply::NoError) {
            emit Checked(false, reply->errorString());
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        Release rel;
        rel.version = o.value("tag_name").toString();
        rel.published = o.value("published_at").toString().left(10);
        for (const QJsonValue& v : o.value("assets").toArray()) {
            const QJsonObject asset = v.toObject();
            const QString name = asset.value("name").toString();
            if (!name.endsWith(".7z", Qt::CaseInsensitive) && !name.endsWith(".zip", Qt::CaseInsensitive)) continue;
            if (!name.contains("decensor", Qt::CaseInsensitive)) continue;
            rel.url = asset.value("browser_download_url").toString();
            rel.size = qint64(asset.value("size").toDouble());
            const QString digest = asset.value("digest").toString();
            if (digest.startsWith("sha256:")) rel.sha256 = digest.mid(7).toLower();
            break;
        }
        if (!rel.valid()) {
            emit Checked(false, tr("The latest release (%1) has no mod archive.").arg(rel.version));
            return;
        }
        latest_ = rel;
        emit Checked(true, {});
    });
}

void Decensor::Install(const QString& modsDir) {
    if (busy_ || !latest_.valid() || modsDir.isEmpty()) return;
    busy_ = installing_ = true;
    modsDir_ = modsDir;
    QDir().mkpath(modsDir);
    partPath_ = modsDir + "/.decensor-download.part";
    part_ = new QFile(partPath_, this);
    if (!part_->open(QIODevice::WriteOnly)) {
        Fail(tr("Could not write %1").arg(partPath_));
        return;
    }
    hash_ = new QCryptographicHash(QCryptographicHash::Sha256);
    QNetworkRequest req{QUrl(latest_.url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    reply_ = net_->get(req);
    connect(reply_, &QNetworkReply::readyRead, this, [this] {
        const QByteArray chunk = reply_->readAll();
        hash_->addData(chunk);
        if (part_->write(chunk) != chunk.size()) {
            Fail(tr("Could not write the download (disk full?)"));
        }
    });
    connect(reply_, &QNetworkReply::downloadProgress, this, [this](qint64 done, qint64 total) {
        emit Progress(tr("Downloading Decensor %1…").arg(latest_.version), done, total > 0 ? total : latest_.size);
    });
    connect(reply_, &QNetworkReply::finished, this, &Decensor::Downloaded);
}

void Decensor::Cancel() {
    if (reply_) reply_->abort();  // finished follows, with OperationCanceledError
}

void Decensor::Fail(const QString& error) {
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
        reply_->deleteLater();
    }
    if (part_) {
        part_->close();
        part_->deleteLater();
        part_ = nullptr;
    }
    delete hash_;
    hash_ = nullptr;
    QFile::remove(partPath_);
    busy_ = installing_ = false;
    emit Finished(false, error);
}

void Decensor::Downloaded() {
    QNetworkReply* reply = reply_;
    if (!reply) return;
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        Fail(reply->error() == QNetworkReply::OperationCanceledError ? tr("Download cancelled.")
                                                                    : tr("Download failed: %1").arg(reply->errorString()));
        return;
    }
    part_->write(reply->readAll());
    part_->close();
    const QString got = QString::fromLatin1(hash_->result().toHex());
    delete hash_;
    hash_ = nullptr;
    if (!latest_.sha256.isEmpty() && got != latest_.sha256) {
        Fail(tr("The download is not the file GitHub lists (SHA-256 differs). Nothing was changed."));
        return;
    }

    // unpacking takes a few seconds: off the window's thread
    emit Progress(tr("Unpacking Decensor %1…").arg(latest_.version), 0, 0);
    const QString staging = modsDir_ + "/.decensor-staging";
    QDir(staging).removeRecursively();
    auto result = std::make_shared<std::pair<int, QString>>(-1, QString());
    const QString archivePath = partPath_;
    QThread* worker = QThread::create([=] { result->first = decensor::Extract(archivePath, staging, &result->second); });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QThread::finished, this, [=, this] {
        part_->deleteLater();
        part_ = nullptr;
        QFile::remove(partPath_);
        if (result->first < 0) {
            QDir(staging).removeRecursively();
            busy_ = installing_ = false;
            emit Finished(false, result->second);
            return;
        }
        // the description and version beside the files
        QFile txt(staging + "/mod.txt");
        if (txt.open(QIODevice::WriteOnly))
            txt.write(tr("Decensor %1 (%2): uncensored single-player subtitles and unbleeped cutscene audio. From "
                         "github.com/%3")
                          .arg(latest_.version, latest_.published, kRepo)
                          .toUtf8());
        txt.close();
        QSaveFile ver(VersionFile(staging));
        if (ver.open(QIODevice::WriteOnly)) {
            ver.write(QJsonDocument(QJsonObject{{"version", latest_.version},
                                                {"published", latest_.published},
                                                {"sha256", got},
                                                {"source", latest_.url}})
                          .toJson());
            ver.commit();
        }
        // swapped in whole: the old copy goes only once the new one is in place
        const QString target = modsDir_ + "/" + kModName;
        const QString old = modsDir_ + "/.decensor-old";
        QDir(old).removeRecursively();
        if (QFileInfo::exists(target) && !QDir().rename(target, old)) {
            QDir(staging).removeRecursively();
            busy_ = installing_ = false;
            emit Finished(false, tr("Could not replace %1 (is a file in it open?)").arg(target));
            return;
        }
        if (!QDir().rename(staging, target)) {
            QDir().rename(old, target);
            busy_ = installing_ = false;
            emit Finished(false, tr("Could not move the new files into %1").arg(target));
            return;
        }
        QDir(old).removeRecursively();
        busy_ = installing_ = false;
        emit Finished(true, tr("Decensor %1 installed (%2 files).").arg(latest_.version).arg(result->first));
    });
    worker->start();
}
