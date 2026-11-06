#pragma once
#include <QMainWindow>
#include <QProcess>

#include <functional>
#include <vector>

#include "config.h"
#include "decensor.h"
#include "keys.h"
#include "mods.h"
#include "online.h"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSlider;
class QStackedWidget;
class SetupPage;
class QSpinBox;
class QTabWidget;
class QVBoxLayout;
class KeyButton;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    void Play();  // saves the settings, applies the mods and starts the game
    void InstallFrom(const QString& iso);  // the setup page, started with this disc image

protected:
    void closeEvent(QCloseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void FitBanner(int windowHeight);

private:
    // a widget tied to a settings.toml entry: shown from the file, written back into it
    struct Bind {
        std::function<void(const Config&)> load;
        std::function<void(Config&)> store;
    };

    QWidget* BuildGraphics();
    QWidget* BuildEnhancements();
    QWidget* BuildAudio();
    QWidget* BuildControls();
    QWidget* BuildMultiplayer();
    QWidget* BuildMods();
    QWidget* BuildLauncher();

    // simple controls on a form, bound to a key
    QCheckBox* Check(QFormLayout* form, const QString& label, const QString& key, bool def, const QString& hint = {});
    QSpinBox* Percent(QFormLayout* form, const QString& label, const QString& key, int min, int max, int def,
                      const QString& suffix = "%");
    QComboBox* IntChoice(QFormLayout* form, const QString& label, const QString& key, const QList<int>& values,
                         const QStringList& texts, int def);
    void Hint(QFormLayout* form, const QString& text);

    QString GameExe() const;
    void LoadAll();    // settings.toml into the widgets
    bool SaveAll();    // the widgets into settings.toml
    void LoadPlayer();
    void StorePlayer();
    QString PlayerPath() const;  // "player1" or "player1.multiplayer", the keys and mouse set shown
    void RefreshMods();
    bool ApplyMods(bool quiet);
    QWidget* BuildDecensor();
    void RefreshDecensor();
    void SetModEnabled(const QString& name, bool on);
    void RefreshGameStatus();
    void SetStatus(const QString& text);
    void ShowMultiplayerInfoDialog();

    Config config_;
    std::vector<Bind> binds_;
    bool loading_ = false;

    QTabWidget* tabs_ = nullptr;
    QStackedWidget* stack_ = nullptr;  // the settings, or the setup page
    SetupPage* setupPage_ = nullptr;
    void ShowSetup(bool rebuild);
    QLabel* status_ = nullptr;
    QPushButton* play_ = nullptr;
    QWidget* banner_ = nullptr;
    QProcess* game_ = nullptr;

    // graphics
    QComboBox* resolution_ = nullptr;
    QComboBox* aspect_ = nullptr;
    QComboBox* gpu_ = nullptr;
    QComboBox* audioDevice_ = nullptr;

    // controls: the player and set shown
    QComboBox* player_ = nullptr;
    QRadioButton* singleSet_ = nullptr;
    QRadioButton* multiSet_ = nullptr;
    int shownPlayer_ = 0;
    bool shownMulti_ = false;
    QCheckBox* useKeyboard_ = nullptr;
    QCheckBox* useMouse_ = nullptr;
    QComboBox* controller_ = nullptr;
    QCheckBox* invertAim_ = nullptr;
    QCheckBox* vibration_ = nullptr;
    QSpinBox* vibrationPercent_ = nullptr;
    QCheckBox* mouseLook_ = nullptr;
    QSpinBox* mouseX_ = nullptr;
    QSpinBox* mouseY_ = nullptr;
    QCheckBox* invertY_ = nullptr;
    QPushButton* copySingle_ = nullptr;
    KeyButton* keyButtons_[keys::kActions][keys::kSlots] = {};

    // mods and launcher
    QListWidget* modList_ = nullptr;
    QLabel* modStatus_ = nullptr;
    QLineEdit* gamePath_ = nullptr;
    QLineEdit* modsPath_ = nullptr;
    QLabel* gameStatus_ = nullptr;
    QLabel* compilerInfo_ = nullptr;
    QPushButton* removeCompiler_ = nullptr;
    QCheckBox* closeOnPlay_ = nullptr;

    // online play: host (the router opens the port, a join code to share) or join with a code
    online::Router* router_ = nullptr;
    QLineEdit* joinHost_ = nullptr;   // the game's host address (join_ip)
    QSpinBox* linkPort_ = nullptr;    // the game's system link port
    QLabel* onlineMode_ = nullptr;
    QWidget* hostPanel_ = nullptr;
    QLabel* hostStatus_ = nullptr;
    QLineEdit* hostCode_ = nullptr;
    QPushButton* copyCode_ = nullptr;
    QPushButton* newCode_ = nullptr;
    QCheckBox* protect_ = nullptr;        // a room key (on by default); off: system link exactly as before
    QLabel* hostDetails_ = nullptr;       // this PC's LAN address, for forwarding by hand: shown on request
    quint32 roomKey_ = 0;                 // the game's system_link_key
    QHostAddress publicIp_;               // the host's, from the router or STUN; never shown
    QWidget* joinPanel_ = nullptr;
    QLineEdit* joinCode_ = nullptr;
    QLabel* joinStatus_ = nullptr;
    void StartHosting();
    void ShowHostCode(const QHostAddress& publicIp, bool portOpen, const QString& detail);
    void MakeRoomCode();  // a new key and code for the known public address
    void RefreshOnlineMode();

    // the decensor mod, kept up to date from its GitHub releases
    Decensor* decensor_ = nullptr;
    QCheckBox* decensorUse_ = nullptr;
    QCheckBox* decensorAuto_ = nullptr;
    QLabel* decensorStatus_ = nullptr;
    QPushButton* decensorCheck_ = nullptr;
    QPushButton* decensorUpdate_ = nullptr;
    QPushButton* decensorCancel_ = nullptr;
    QProgressBar* decensorProgress_ = nullptr;
    QString decensorNote_;           // the last check's or install's outcome
    bool decensorInstallWhenChecked_ = false;
};
