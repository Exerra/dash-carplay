#pragma once
#ifdef DASH_CARPLAY
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>

namespace CarPlayTouch {
bool dark(const QWidget *widget);
QColor accent(const QWidget *widget);
void theme(QWidget *widget, bool dark);
void scroll(QWidget *viewport);
QPushButton *button(const QString &text, const char *name, QWidget *parent, bool primary = false);
bool editText(QWidget *parent, const QString &title, QString &value, bool password = false, int maximum = 256);
bool confirm(QWidget *parent, const QString &title, const QString &message, const QString &action);
}

class CarPlayTouchChoice : public QComboBox {
 public:
    CarPlayTouchChoice(const QString &title, QWidget *parent) : QComboBox(parent), title_(title) { setMinimumHeight(60); }
    void showPopup() override;
 protected:
    void paintEvent(QPaintEvent *event) override;
 private:
    QString title_;
};

class CarPlayTouchEdit : public QLineEdit {
 public:
    CarPlayTouchEdit(const QString &value, const QString &title, QWidget *parent);
 protected:
    void mouseReleaseEvent(QMouseEvent *event) override;
 private:
    QString title_;
};

class CarPlayTouchNumber : public QSpinBox {
 public:
    explicit CarPlayTouchNumber(QWidget *parent);
 protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};
#endif
