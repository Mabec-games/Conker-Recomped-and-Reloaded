#pragma once
// Controller navigation for the launcher, for a Steam Deck in Game Mode (or any couch setup), where Steam hands the
// launcher a gamepad and no mouse or keyboard. SDL3 reads the pads; their buttons move Qt's keyboard focus and press
// what it is on:
//   D-pad / left stick   up and down move between controls; left and right change a choice, slider or number, or
//                        move along a row of buttons; in a list (a drop-down's too) they move the selection
//   A                    presses a button, ticks a box, opens a drop-down, picks the item in a list
//   B                    Escape: closes a drop-down or a dialog
//   LB / RB              the previous / next tab
//   Start                the page's main button (Play, or Install on the setup page): a QPushButton named "primary"
// An orange ring shows the focus once a pad is used, and goes away with the mouse. Pads are only read while a launcher
// window is active, and let go while the game runs, so the two never read one at once.
#include <QObject>
#include <QPointer>

#include <cstdint>

class QTimer;
class QWidget;

class Gamepad : public QObject {
    Q_OBJECT
public:
    explicit Gamepad(QObject* parent = nullptr);
    ~Gamepad() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum Dir { kUp, kDown, kLeft, kRight, kDirs };

    void Poll();
    bool Open();   // SDL's gamepad subsystem, with the pads already plugged in
    void Close();  // all of it, so the game has the pads to itself
    void Button(int button);
    void Move(Dir d);
    void FocusStep(bool next);
    void Tab(int step);
    void Press();
    void Key(int key);
    void UpdateRing();

    QTimer* timer_;
    bool open_ = false;
    bool held_[kDirs] = {};
    uint64_t repeatAt_[kDirs] = {};
    QPointer<QWidget> ring_;
    bool ringOn_ = false;
};
