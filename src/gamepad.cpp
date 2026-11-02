#include "gamepad.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>

#include <SDL3/SDL.h>

#include <algorithm>

namespace {

constexpr int kPollMs = 16;
constexpr uint64_t kRepeatDelayMs = 400, kRepeatMs = 110, kRetryMs = 2000;
constexpr int16_t kStick = 16000;  // of 32767: past this the left stick counts as a direction

// the widget keys go to: an open drop-down's list first, else the focus
QWidget* Focus() {
    if (QWidget* popup = QApplication::activePopupWidget()) return popup->focusWidget() ? popup->focusWidget() : popup;
    return QApplication::focusWidget();
}

QWidget* ActiveWindow() {
    if (QWidget* popup = QApplication::activePopupWidget()) return popup;
    if (QWidget* modal = QApplication::activeModalWidget()) return modal;
    return QApplication::activeWindow();
}

bool InPopup(const QWidget* w) { return w && (w->window()->windowFlags() & Qt::Popup) == Qt::Popup; }

// a text view the stick scrolls, leaving it at either end
QScrollBar* TextScroll(QWidget* w) {
    if (auto* t = qobject_cast<QPlainTextEdit*>(w); t && t->isReadOnly()) return t->verticalScrollBar();
    if (auto* t = qobject_cast<QTextEdit*>(w); t && t->isReadOnly()) return t->verticalScrollBar();
    return nullptr;
}

// the orange ring around the focus, over the window's other widgets and clicks passing through it
class Ring : public QWidget {
public:
    explicit Ring(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(0xf5, 0x7c, 0x2c), 3));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), 5, 5);
    }
};

}  // namespace

Gamepad::Gamepad(QObject* parent) : QObject(parent), timer_(new QTimer(this)) {
    connect(timer_, &QTimer::timeout, this, &Gamepad::Poll);
    timer_->start(kPollMs);
    qApp->installEventFilter(this);
    // a control the pad moves to in a scrolled page comes into view (Tab does this in QScrollArea, setFocus doesn't)
    connect(qApp, &QApplication::focusChanged, this, [](QWidget*, QWidget* now) {
        for (QWidget* w = now ? now->parentWidget() : nullptr; w; w = w->parentWidget())
            if (auto* area = qobject_cast<QScrollArea*>(w)) {
                if (area->widget() && area->widget()->isAncestorOf(now)) area->ensureWidgetVisible(now, 24, 48);
                break;
            }
    });
}

Gamepad::~Gamepad() { Close(); }

bool Gamepad::Open() {
    static uint64_t next_try = 0;
    const uint64_t now = SDL_GetTicks();
    if (now < next_try) return false;
    // Qt has the window, so SDL hears the pads without one of its own; HIDAPI stays off so the launcher never takes a
    // pad's raw device (Steam's virtual pad, XInput and the kernel's drivers cover what it needs)
    // and SDL leaves SIGINT and SIGTERM alone: its handlers only queue an SDL quit event, so the launcher would no
    // longer close when asked (by Steam, or a logout)
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        next_try = now + kRetryMs;
        return false;
    }
    open_ = true;  // the pads already plugged in come as SDL_EVENT_GAMEPAD_ADDED, opened in Poll
    return true;
}

void Gamepad::Close() {
    if (!open_) return;
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);  // closes every pad
    open_ = false;
    std::fill(std::begin(held_), std::end(held_), false);
}

void Gamepad::Poll() {
    UpdateRing();
    if (!ActiveWindow()) {  // the game is running, or another program has the focus
        Close();
        return;
    }
    if (!open_ && !Open()) return;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_EVENT_GAMEPAD_ADDED: SDL_OpenGamepad(e.gdevice.which); break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                if (SDL_Gamepad* pad = SDL_GetGamepadFromID(e.gdevice.which)) SDL_CloseGamepad(pad);
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN: Button(e.gbutton.button); break;
            default: break;
        }
        if (!open_) return;  // a button closed the launcher's last window
    }

    // directions from every pad's D-pad and left stick, repeating while held
    bool now[kDirs] = {};
    int count = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
        for (int i = 0; i < count; i++) {
            SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
            if (!pad) continue;
            const int16_t x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
            const int16_t y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
            now[kUp] |= SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -kStick;
            now[kDown] |= SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > kStick;
            now[kLeft] |= SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -kStick;
            now[kRight] |= SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > kStick;
        }
        SDL_free(ids);
    }
    const uint64_t t = SDL_GetTicks();
    for (int d = 0; d < kDirs; d++) {
        if (now[d] && !held_[d]) {
            repeatAt_[d] = t + kRepeatDelayMs;
            Move(Dir(d));
        } else if (now[d] && t >= repeatAt_[d]) {
            repeatAt_[d] = t + kRepeatMs;
            Move(Dir(d));
        }
        held_[d] = now[d];
    }
}

void Gamepad::Button(int button) {
    switch (button) {
        case SDL_GAMEPAD_BUTTON_SOUTH: ringOn_ = true; Press(); break;
        case SDL_GAMEPAD_BUTTON_EAST: ringOn_ = true; Key(Qt::Key_Escape); break;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: ringOn_ = true; Tab(-1); break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: ringOn_ = true; Tab(1); break;
        case SDL_GAMEPAD_BUTTON_START:
            if (QWidget* w = ActiveWindow(); w && !QApplication::activePopupWidget())
                for (QPushButton* b : w->findChildren<QPushButton*>("primary"))
                    if (b->isVisible() && b->isEnabled()) {
                        b->click();
                        break;
                    }
            break;
        default: break;
    }
}

void Gamepad::Move(Dir d) {
    ringOn_ = true;
    QWidget* w = Focus();
    if (!w) {
        FocusStep(true);
        return;
    }
    const bool vertical = d == kUp || d == kDown;
    const int step = d == kDown || d == kRight ? 1 : -1;

    if (auto* view = qobject_cast<QAbstractItemView*>(w)) {
        // a list: the selection moves, and off its top or bottom the focus does (not in a drop-down's own list)
        const QModelIndex cur = view->currentIndex();
        const bool edge = vertical && cur.isValid() && !cur.sibling(cur.row() + step, cur.column()).isValid();
        if (edge && !InPopup(view)) FocusStep(step > 0);
        else Key(d == kUp ? Qt::Key_Up : d == kDown ? Qt::Key_Down : d == kLeft ? Qt::Key_Left : Qt::Key_Right);
        return;
    }
    if (vertical) {
        if (QScrollBar* bar = TextScroll(w); bar && (step > 0 ? bar->value() < bar->maximum() : bar->value() > bar->minimum())) {
            bar->setValue(bar->value() + step * bar->singleStep() * 3);
            return;
        }
        FocusStep(step > 0);
        return;
    }
    if (auto* combo = qobject_cast<QComboBox*>(w); combo && combo->count()) {
        combo->setCurrentIndex(std::clamp(combo->currentIndex() + step, 0, combo->count() - 1));
    } else if (auto* slider = qobject_cast<QAbstractSlider*>(w)) {
        slider->triggerAction(step > 0 ? QAbstractSlider::SliderSingleStepAdd : QAbstractSlider::SliderSingleStepSub);
    } else if (auto* spin = qobject_cast<QAbstractSpinBox*>(w)) {
        spin->stepBy(step);
    } else if (auto* bar = qobject_cast<QTabBar*>(w)) {
        if (const int i = bar->currentIndex() + step; i >= 0 && i < bar->count()) bar->setCurrentIndex(i);
    } else if (qobject_cast<QLineEdit*>(w)) {
        Key(step > 0 ? Qt::Key_Right : Qt::Key_Left);
    } else {
        FocusStep(step > 0);  // along a row of buttons
    }
}

// the next or previous control in the window's tab order that can take the focus now
void Gamepad::FocusStep(bool next) {
    QWidget* window = ActiveWindow();
    if (!window) return;
    QWidget* start = QApplication::focusWidget();
    if (!start || start->window() != window) start = window;
    QWidget* w = start;
    for (int guard = 0; guard < 4000; guard++) {
        w = next ? w->nextInFocusChain() : w->previousInFocusChain();
        if (!w || w == start) return;
        if (w->window() != window || w->focusProxy() || !w->isVisible() || !w->isEnabled() ||
            !(w->focusPolicy() & Qt::TabFocus))
            continue;
        w->setFocus(next ? Qt::TabFocusReason : Qt::BacktabFocusReason);
        return;
    }
}

void Gamepad::Tab(int step) {
    if (QApplication::activePopupWidget()) return;
    QTabWidget* tabs = nullptr;
    for (QWidget* w = Focus(); w && !tabs; w = w->parentWidget()) tabs = qobject_cast<QTabWidget*>(w);
    if (!tabs)
        if (QWidget* window = ActiveWindow())
            for (QTabWidget* t : window->findChildren<QTabWidget*>())
                if (t->isVisible()) {
                    tabs = t;
                    break;
                }
    if (!tabs || tabs->count() < 2) return;
    int i = tabs->currentIndex();
    for (int n = 0; n < tabs->count(); n++) {
        i = (i + step + tabs->count()) % tabs->count();
        if (tabs->isTabEnabled(i)) break;
    }
    tabs->setCurrentIndex(i);
    tabs->tabBar()->setFocus(Qt::TabFocusReason);
}

void Gamepad::Press() {
    QWidget* w = Focus();
    if (!w) return;
    if (auto* button = qobject_cast<QAbstractButton*>(w)) {
        if (button->isEnabled()) button->click();
    } else if (auto* combo = qobject_cast<QComboBox*>(w)) {
        combo->showPopup();
    } else if (auto* view = qobject_cast<QAbstractItemView*>(w)) {
        const QModelIndex cur = view->currentIndex();
        if (!InPopup(view) && cur.isValid() && (cur.flags() & Qt::ItemIsUserCheckable)) {
            const bool on = cur.data(Qt::CheckStateRole).toInt() == Qt::Checked;
            view->model()->setData(cur, on ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
        } else {
            Key(Qt::Key_Return);
        }
    } else if (!qobject_cast<QLineEdit*>(w)) {
        Key(Qt::Key_Space);
    }
}

void Gamepad::Key(int key) {
    QWidget* w = Focus();
    if (!w) w = ActiveWindow();
    if (!w) return;
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    const QPointer<QWidget> alive = w;  // the key may close it
    QApplication::sendEvent(w, &press);
    if (alive) QApplication::sendEvent(alive, &release);
}

void Gamepad::UpdateRing() {
    QWidget* w = ringOn_ ? Focus() : nullptr;
    if (w && (!w->isVisible() || InPopup(w))) w = nullptr;  // a drop-down's list shows its own selection
    if (!w) {
        if (ring_) ring_->hide();
        return;
    }
    QWidget* window = w->window();
    if (!ring_ || ring_->parentWidget() != window) {
        delete ring_;
        ring_ = new Ring(window);
    }
    // the part of the control that shows (a scrolled page cuts it), a little bigger
    const QRect seen = w->visibleRegion().boundingRect();
    if (seen.isEmpty()) {
        ring_->hide();
        return;
    }
    const QRect r = QRect(w->mapTo(window, seen.topLeft()), seen.size()).adjusted(-3, -3, 3, 3);
    if (ring_->geometry() != r || !ring_->isVisible()) {
        ring_->setGeometry(r);
        ring_->show();
        ring_->raise();
    }
}

bool Gamepad::eventFilter(QObject* watched, QEvent* event) {
    // the mouse (or a touch, which Qt also sends as one) takes over: the ring goes until a pad is used again
    // (only the real one: Qt makes up moves of its own as widgets shift under the pointer)
    if (ringOn_ && event->spontaneous() &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove) && !dynamic_cast<Ring*>(watched))
        ringOn_ = false;
    return QObject::eventFilter(watched, event);
}
