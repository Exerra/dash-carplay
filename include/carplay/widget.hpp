#pragma once
#ifdef DASH_CARPLAY
#include "carplay/client.hpp"
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QWidget>

class CarPlayWidget : public QWidget {
    Q_OBJECT
 public:
    explicit CarPlayWidget(QWidget *parent = nullptr);
    ~CarPlayWidget() override;
    void setAndroidAutoActive(bool active);
    void setNightMode(bool night) { client_->setNightMode(night); }
    void setVolume(int percent) { client_->setVolume(percent); }
 private:
    bool androidAutoActive_ = false;
    void init();
    CarPlayClient *client_ = nullptr;
    QProcess process_;
    QTimer poll_;
    QLabel *status_ = nullptr;
    QWidget *settings_ = nullptr;
    QLineEdit *ssid_ = nullptr;
    QLineEdit *password_ = nullptr;
    QLineEdit *address_ = nullptr;
    QLineEdit *identity_ = nullptr;
    QPushButton *start_ = nullptr;
    QPushButton *stop_ = nullptr;
    QString stateDirectory_;
    void startReceiver();
    void stopReceiver();
};
#endif
