#include "setuppage.h"

#include <algorithm>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>

#include <memory>
#include <thread>

#include "install.h"

namespace {

constexpr qint64 kBuildSpace = qint64(3) << 30;  // the compiler, generated code and objects while building

QString Problem(const QString& text) {
    return "<span style='color:#e5484d'><b>✘ " + text.toHtmlEscaped() + "</b></span>";
}

QString Size(qint64 bytes) { return QString::number(double(bytes) / 1e9, 'f', 1) + " GB"; }
QString Mb(qint64 bytes) { return QString::number((bytes + 500000) / 1000000) + " MB"; }

QString OfflineHelp() {
    return QObject::tr("Setup downloads the compiler it builds the game with (%1, %2) from ziglang.org once, and "
                       "couldn't reach it. Connect to the internet and press the button again, or install offline: "
                       "download %3 on another computer and put it in %4, next to the launcher.")
        .arg(install::CompilerArchiveName(), Mb(install::CompilerDownloadBytes()), install::CompilerUrl(),
             QDir::toNativeSeparators(install::Root()));
}

}  // namespace

SetupPage::SetupPage(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(24, 8, 24, 16);
    title_ = new QLabel;
    QFont tf = title_->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.5);
    tf.setBold(true);
    title_->setFont(tf);
    v->addWidget(title_);
    intro_ = new QLabel;
    intro_->setWordWrap(true);
    v->addWidget(intro_);
    v->addSpacing(8);

    auto* row = new QHBoxLayout;
    iso_ = new QLineEdit;
    iso_->setPlaceholderText(tr("Your Conker: Live & Reloaded disc image (.iso)"));
    browse_ = new QPushButton(tr("Choose disc image…"));
    row->addWidget(iso_, 1);
    row->addWidget(browse_);
    v->addLayout(row);
    check_ = new QLabel;
    check_->setWordWrap(true);
    check_->setTextFormat(Qt::RichText);
    v->addWidget(check_);
    download_ = new QLabel;
    download_->setWordWrap(true);
    v->addWidget(download_);
    v->addSpacing(8);

    step_ = new QLabel;
    step_->setWordWrap(true);
    v->addWidget(step_);
    bar_ = new QProgressBar;
    bar_->setRange(0, 100);
    bar_->setVisible(false);
    v->addWidget(bar_);
    details_ = new QToolButton;
    details_->setText(tr("Details"));
    details_->setCheckable(true);
    details_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    details_->setArrowType(Qt::RightArrow);
    details_->setAutoRaise(true);
    v->addWidget(details_);
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(2000);
    log_->setVisible(false);
    QFont mono("monospace");
    mono.setStyleHint(QFont::Monospace);
    log_->setFont(mono);
    v->addWidget(log_, 1);
    v->addStretch(1);

    auto* bar = new QHBoxLayout;
    back_ = new QPushButton(tr("Back"));
    cancel_ = new QPushButton(tr("Cancel"));
    start_ = new QPushButton(tr("Install"));
    start_->setMinimumSize(160, 44);
    QFont sf = start_->font();
    sf.setBold(true);
    sf.setPointSizeF(sf.pointSizeF() * 1.2);
    start_->setFont(sf);
    start_->setStyleSheet(
        "QPushButton { background: #e8681a; color: white; border: none; border-radius: 6px; padding: 6px 24px; }"
        "QPushButton:hover { background: #f57c2c; }"
        "QPushButton:disabled { background: #7a5a46; color: #ddd; }");
    bar->addWidget(back_);
    bar->addStretch(1);
    bar->addWidget(cancel_);
    bar->addWidget(start_);
    v->addLayout(bar);

    connect(details_, &QToolButton::toggled, this, [this](bool on) {
        log_->setVisible(on);
        details_->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    });
    connect(browse_, &QPushButton::clicked, this, [this] {
        const QString start = iso_->text().isEmpty() ? QDir::homePath() : QFileInfo(iso_->text()).absolutePath();
        const QString picked = QFileDialog::getOpenFileName(this, tr("Choose your disc image"), start,
                                                            tr("Xbox disc images (*.iso *.ISO);;All files (*)"));
        if (picked.isEmpty()) return;
        iso_->setText(QDir::toNativeSeparators(picked));
        Refresh();
    });
    connect(iso_, &QLineEdit::editingFinished, this, &SetupPage::Refresh);
    connect(start_, &QPushButton::clicked, this, &SetupPage::Start);
    connect(back_, &QPushButton::clicked, this, &SetupPage::Back);
    connect(cancel_, &QPushButton::clicked, this, [this] {
        stop_ = true;
        if (setup_) setup_->kill();
        Step(tr("Cancelling…"), -1);
    });
}

bool SetupPage::AssetsReady() const {
    QFile xbe(install::Assets() + "/default.xbe");
    if (!xbe.open(QIODevice::ReadOnly)) return false;
    return install::SupportedXbe(QString::fromLatin1(QCryptographicHash::hash(xbe.readAll(), QCryptographicHash::Sha1).toHex()));
}

void SetupPage::Prepare(bool rebuild) {
    rebuild_ = rebuild;
    title_->setText(rebuild ? tr("Rebuild the game") : tr("Welcome! Let's set up the game"));
    if (AssetsReady())
        intro_->setText(tr("The game's files are already in %1. Press %2 to build the game from them; this takes a "
                           "few minutes. To extract a disc image again instead, choose it below.")
                            .arg(QDir::toNativeSeparators(install::Assets()), rebuild ? tr("Rebuild") : tr("Install")));
    else
        intro_->setText(tr("Choose your own Conker: Live & Reloaded disc image (original Xbox, USA). The launcher "
                           "extracts it into its own folder and builds the game from it, which takes a few minutes "
                           "and needs about 8 GB free while it works. Everything stays in:\n%1\n\nThe disc image is "
                           "only read, and is not needed afterwards.")
                            .arg(QDir::toNativeSeparators(install::Root())));
    // the files are there: building from them needs no disc image (choosing one extracts it again)
    if (AssetsReady()) iso_->clear();
    else if (iso_->text().isEmpty())
        iso_->setText(QSettings(install::IniFile(), QSettings::IniFormat).value("setup/iso").toString());
    back_->setVisible(rebuild);
    step_->clear();
    bar_->setVisible(false);
    Refresh();
}

void SetupPage::StartWith(const QString& iso) {
    iso_->setText(iso);
    Refresh();
    Start();
}

void SetupPage::Refresh() {
    const QString iso = QDir::fromNativeSeparators(iso_->text().trimmed());
    bool ready = AssetsReady() && iso.isEmpty();
    if (!iso.isEmpty()) {
        const install::Disc d = install::CheckIso(iso);
        if (d.ok) {
            check_->setText(tr("✔ Conker: Live & Reloaded (USA), %1 files, %2.").arg(d.files).arg(Size(d.bytes)));
            ready = true;
        } else {
            check_->setText(Problem(d.error));
        }
    } else {
        check_->setText(AssetsReady() ? tr("✔ The game's files are already extracted.") : QString());
    }
    const bool tool = QFileInfo(install::SetupTool()).isFile();
    if (!tool)
        check_->setText(Problem(tr("The build kit (%1) is missing from this folder, so the game can't be built here. "
                                   "Run the launcher as it came: the AppImage, or ConkerLauncher next to %1.")
                                    .arg(QFileInfo(install::SetupTool()).fileName())));
    if (!install::BundledCompiler().isEmpty())
        download_->setText(tr("Downloads: none. The compiler that builds the game is included with the launcher, so "
                              "setup works offline."));
    else if (install::CompilerReady())
        download_->setText(tr("Downloads: none. The compiler is already in %1.")
                               .arg(QDir::toNativeSeparators(install::WorkDir())));
    else if (!install::OfflineCompiler().isEmpty())
        download_->setText(tr("Downloads: none. The compiler is taken from %1 next to the launcher.")
                               .arg(install::CompilerArchiveName()));
    else
        download_->setText(tr("Downloads: the compiler the game is built with, %1 from ziglang.org, once (it is kept "
                              "for rebuilds). Needs an internet connection, or put %2 next to the launcher to "
                              "install offline.").arg(Mb(install::CompilerDownloadBytes()), install::CompilerArchiveName()));
    start_->setText(rebuild_ ? tr("Rebuild") : tr("Install"));
    start_->setEnabled(!running_ && ready && tool);
    cancel_->setVisible(running_);
    browse_->setEnabled(!running_);
    iso_->setEnabled(!running_);
    back_->setEnabled(!running_);
}

void SetupPage::Log(const QString& text) {
    log_->appendPlainText(text);
}

void SetupPage::Step(const QString& text, int percent) {
    step_->setText(text);
    bar_->setVisible(true);
    if (percent < 0) {
        bar_->setRange(0, 0);
    } else {
        bar_->setRange(0, 100);
        bar_->setValue(percent);
    }
}

void SetupPage::Start() {
    if (running_) return;
    const QString iso = QDir::fromNativeSeparators(iso_->text().trimmed());
    stop_ = false;
    log_->clear();

    // the tools the kit runs
#ifndef Q_OS_WIN
    for (const char* tool : {"curl", "tar"})
        if (QStandardPaths::findExecutable(tool).isEmpty()) {
            Finish(false, tr("Setup needs the '%1' program. Install it with your package manager and try again.").arg(tool));
            return;
        }
#endif

    qint64 need = kBuildSpace;
    if (!iso.isEmpty()) {
        const install::Disc d = install::CheckIso(iso);
        if (!d.ok) {
            Finish(false, d.error);
            return;
        }
        need += d.bytes;
        QSettings(install::IniFile(), QSettings::IniFormat).setValue("setup/iso", iso);
    }
    const QStorageInfo disk(install::Root());
    if (disk.bytesAvailable() < need) {
        Finish(false, tr("Not enough free space on %1: %2 needed, %3 free.")
                          .arg(disk.rootPath(), Size(need), Size(disk.bytesAvailable())));
        return;
    }

    running_ = true;
    Refresh();
    PrepareCompiler([this, iso] {
        if (!iso.isEmpty()) Extract(iso);
        else RunSetup();
    });
}

void SetupPage::PrepareCompiler(const std::function<void()>& next) {
    if (!install::BundledCompiler().isEmpty() || install::CompilerReady()) {
        next();
        return;
    }
    if (const QString archive = install::OfflineCompiler(); !archive.isEmpty()) {
        Step(tr("Unpacking the compiler from %1…").arg(QFileInfo(archive).fileName()), -1);
        Log(tr("Using %1 (no download)").arg(archive));
        auto error = std::make_shared<QString>();
        auto ok = std::make_shared<bool>(false);
        QThread* worker = QThread::create([=] { *ok = install::UnpackCompiler(archive, error.get()); });
        connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        connect(worker, &QThread::finished, this, [=, this] {
            if (*ok) next();
            else Finish(false, *error);
        });
        worker->start();
        return;
    }
    // the kit downloads it: is ziglang.org there? (asked before 5 GB are extracted, not after)
    Step(tr("Checking the connection to ziglang.org…"), -1);
    auto* net = new QNetworkAccessManager(this);
    QNetworkRequest req{QUrl(install::CompilerUrl())};
    req.setTransferTimeout(15000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = net->head(req);
    connect(reply, &QNetworkReply::finished, this, [=, this] {
        const bool ok = reply->error() == QNetworkReply::NoError;
        const QString why = reply->errorString();
        reply->deleteLater();
        net->deleteLater();
        if (stop_) {
            Finish(false, tr("Cancelled."));
        } else if (!ok) {
            Log(tr("ziglang.org: %1").arg(why));
            Finish(false, OfflineHelp());
        } else {
            next();
        }
    });
}

void SetupPage::Extract(const QString& iso) {
    Step(tr("Extracting the disc image…"), 0);
    Log(tr("Extracting %1 into %2").arg(iso, install::Assets()));
    const QString part = install::Assets() + ".part";
    QDir(part).removeRecursively();
    auto result = std::make_shared<QString>();
    auto ok = std::make_shared<bool>(false);
    QThread* worker = QThread::create([=, this] {
        *ok = install::ExtractIso(iso, part, [this](qint64 done, qint64 total) {
            static thread_local int last = -1;
            const int pct = int(done * 100 / std::max<qint64>(total, 1));
            if (pct == last) return;
            last = pct;
            QMetaObject::invokeMethod(this, [this, pct] { Step(tr("Extracting the disc image… %1%").arg(pct), pct * 35 / 100); });
        }, stop_, result.get());
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QThread::finished, this, [=, this] {
        if (!*ok) {
            QDir(part).removeRecursively();
            Finish(false, *result);
            return;
        }
        // the new files replace any old ones whole
        QDir(install::Assets()).removeRecursively();
        if (!QDir().rename(part, install::Assets())) {
            Finish(false, tr("Could not move the extracted files into %1").arg(install::Assets()));
            return;
        }
        Log(tr("Extracted."));
        RunSetup();
    });
    worker->start();
}

void SetupPage::RunSetup() {
    if (stop_) {
        Finish(false, tr("Cancelled."));
        return;
    }
    Step(tr("Preparing the build…"), 35);
    install::MakePortable();
    setup_ = new QProcess(this);
    setup_->setProcessChannelMode(QProcess::MergedChannels);
    setup_->setWorkingDirectory(install::Root());
    const QStringList args = {"--cli",
                              "--source", install::Assets(),
                              "--work", install::WorkDir(),
                              "--game-dir", install::GameDir(),
                              "--jobs", QString::number(std::max(1u, std::thread::hardware_concurrency()))};
    const QString bundled = install::BundledCompiler();
    const QStringList toolchain = bundled.isEmpty() ? QStringList() : QStringList{"--toolchain", bundled};
    Log(QFileInfo(install::SetupTool()).fileName() + " " + (args + toolchain).join(' '));
    connect(setup_, &QProcess::readyRead, this, &SetupPage::SetupOutput);
    connect(setup_, &QProcess::finished, this, [this, bundled](int code, QProcess::ExitStatus status) {
        SetupOutput();
        setup_->deleteLater();
        setup_ = nullptr;
        if (stop_) {
            Finish(false, tr("Cancelled."));
        } else if ((status != QProcess::NormalExit || code != 0) && bundled.isEmpty() && !install::CompilerReady()) {
            Finish(false, OfflineHelp());  // the connection went during the download
        } else if (status != QProcess::NormalExit || code != 0 || !install::GameBuilt()) {
            Finish(false, tr("The build did not finish. Open Details for what it said; its logs are in %1.")
                              .arg(QDir::toNativeSeparators(install::WorkDir() + "/logs")));
        } else {
            // the kit removes the generated code and its own files, and keeps a downloaded compiler for rebuilds;
            // with the shipped one there is nothing worth keeping (only Zig's cache)
            if (!bundled.isEmpty()) QDir(install::WorkDir()).removeRecursively();
            install::MarkKitBuilt();
            Finish(true, tr("The game is ready."));
        }
    });
    connect(setup_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            setup_->deleteLater();
            setup_ = nullptr;
            Finish(false, tr("Could not start %1").arg(install::SetupTool()));
        }
    });
    setup_->start(install::SetupTool(), args + toolchain);
    setup_->closeWriteChannel();  // nothing to answer: a prompt would read the end of input
}

// the kit's progress lines: the compiler download (curl's bar, "12.3%"), then three steps, the second counting parts
void SetupPage::SetupOutput() {
    if (!setup_) return;
    pending_ += setup_->readAll();
    static const QRegularExpression percent(R"((\d{1,3}(?:\.\d)?)%\s*$)");
    static const QRegularExpression parts(R"(^\s*(\d+)\s*/\s*(\d+)\s*$)");
    for (;;) {
        qsizetype cut = -1;
        for (qsizetype i = 0; i < pending_.size(); i++)
            if (pending_[i] == '\n' || pending_[i] == '\r') {
                cut = i;
                break;
            }
        if (cut < 0) break;
        const bool carriage = pending_[cut] == '\r';
        const QString line = QString::fromUtf8(pending_.left(cut)).trimmed();
        pending_.remove(0, cut + 1);
        if (line.isEmpty()) continue;
        if (auto m = parts.match(line); m.hasMatch()) {
            const int n = m.captured(1).toInt(), total = std::max(1, m.captured(2).toInt());
            Step(tr("Building the game… part %1 of %2").arg(n).arg(total), 52 + 43 * n / total);
            continue;
        }
        if (line.contains('#')) {  // curl's progress bar
            if (auto m = percent.match(line); m.hasMatch())
                Step(tr("Downloading the compiler… %1%").arg(int(m.captured(1).toDouble())),
                     36 + int(m.captured(1).toDouble() / 100.0 * 10));
            continue;
        }
        if (!carriage) Log(line);
        if (line.startsWith("Downloading the tools")) Step(tr("Downloading the compiler…"), 36);
        else if (line.startsWith("Unpacking the downloaded tools")) Step(tr("Unpacking the compiler…"), 47);
        else if (line.startsWith("Step 1 of 3")) Step(tr("Converting the game's code…"), 49);
        else if (line.startsWith("Step 2 of 3")) Step(tr("Building the game…"), 52);
        else if (line.startsWith("Step 3 of 3")) Step(tr("Putting the game together…"), 96);
    }
}

void SetupPage::Finish(bool ok, const QString& message) {
    running_ = false;
    if (ok) {
        Step(message, 100);
        Log(message);
        Refresh();
        emit Done();
        return;
    }
    bar_->setVisible(false);
    step_->setText("<b>" + message.toHtmlEscaped() + "</b>");
    Log(message);
    Refresh();
}
