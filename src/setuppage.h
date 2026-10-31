#pragma once
// First run (and rebuilds): the player picks their disc image, the launcher extracts it into game/assets and has the
// build kit (setup-linux) recompile the game into game/. Everything stays in the launcher's folder.
#include <QWidget>

#include <atomic>
#include <functional>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QProgressBar;
class QPushButton;
class QToolButton;

class SetupPage : public QWidget {
    Q_OBJECT
public:
    explicit SetupPage(QWidget* parent = nullptr);
    // rebuild: the game is there already and this builds it again (Cancel goes back)
    void Prepare(bool rebuild);
    bool Running() const { return running_; }
    void StartWith(const QString& iso);  // as if the player chose it and pressed Install

signals:
    void Done();
    void Back();

private:
    void Start();
    // the compiler in place before anything else: already there, unpacked from an archive beside the launcher, or
    // ziglang.org reachable for the kit to download it; then next()
    void PrepareCompiler(const std::function<void()>& next);
    void Extract(const QString& iso);
    void RunSetup();
    void SetupOutput();
    void Finish(bool ok, const QString& message);
    void Refresh();
    void Log(const QString& text);
    void Step(const QString& text, int percent);  // percent -1: busy
    bool AssetsReady() const;  // game/assets holds the supported game already

    QLabel* title_ = nullptr;
    QLabel* intro_ = nullptr;
    QLineEdit* iso_ = nullptr;
    QPushButton* browse_ = nullptr;
    QLabel* check_ = nullptr;
    QLabel* download_ = nullptr;  // what will be downloaded
    QLabel* step_ = nullptr;
    QProgressBar* bar_ = nullptr;
    QToolButton* details_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    QPushButton* start_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QPushButton* back_ = nullptr;

    QProcess* setup_ = nullptr;
    QByteArray pending_;  // setup output not yet a whole line
    std::atomic<bool> stop_{false};
    bool running_ = false;
    bool rebuild_ = false;
};
