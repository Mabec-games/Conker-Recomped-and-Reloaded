#pragma once
// A key binding slot: click, then press a key or mouse button. Esc cancels, right click clears.
#include <QPushButton>

class KeyButton : public QPushButton {
    Q_OBJECT
public:
    explicit KeyButton(QWidget* parent = nullptr);
    void SetKey(const QString& name);  // "" none
    QString Key() const { return key_; }

signals:
    void Changed();

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;

private:
    void Start();
    void Stop();
    void Show();

    QString key_;
    bool capturing_ = false;
};
