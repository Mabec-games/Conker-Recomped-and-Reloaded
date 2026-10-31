#include "keybutton.h"

#include <QKeyEvent>
#include <QMouseEvent>

#include "keys.h"

KeyButton::KeyButton(QWidget* parent) : QPushButton(parent) {
    setMinimumWidth(130);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip(tr("Click, then press a key or mouse button. Esc cancels, right click clears."));
    Show();
}

void KeyButton::SetKey(const QString& name) {
    key_ = name;
    Show();
}

void KeyButton::Show() {
    if (capturing_) setText(tr("Press a key…"));
    else setText(key_.isEmpty() ? QStringLiteral("—") : key_);
}

void KeyButton::Start() {
    capturing_ = true;
    setDown(true);
    Show();
    grabKeyboard();
    grabMouse();
}

void KeyButton::Stop() {
    if (!capturing_) return;
    capturing_ = false;
    releaseKeyboard();
    releaseMouse();
    setDown(false);
    Show();
}

void KeyButton::keyPressEvent(QKeyEvent* e) {
    if (!capturing_) {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Space) {
            Start();
            return;
        }
        if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
            key_.clear();
            Show();
            emit Changed();
            return;
        }
        QPushButton::keyPressEvent(e);
        return;
    }
    if (e->isAutoRepeat()) return;
    if (e->key() == Qt::Key_Escape) {
        Stop();
        return;
    }
    const QString name = keys::FromKey(e);
    if (name.isEmpty()) return;  // a key the game cannot bind: keep waiting
    key_ = name;
    Stop();
    emit Changed();
}

void KeyButton::mousePressEvent(QMouseEvent* e) {
    if (!capturing_) {
        if (e->button() == Qt::LeftButton) Start();
        else if (e->button() == Qt::RightButton) {
            key_.clear();
            Show();
            emit Changed();
        }
        return;
    }
    const QString name = keys::FromMouse(e);
    if (name.isEmpty()) return;
    key_ = name;
    Stop();
    emit Changed();
}

void KeyButton::focusOutEvent(QFocusEvent* e) {
    Stop();
    QPushButton::focusOutEvent(e);
}
