#pragma once
#ifdef DASH_CARPLAY
#include "carplay/client.hpp"
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QStackedWidget>

class CarPlayWidget : public QWidget {
    Q_OBJECT
 public:
    explicit CarPlayWidget(QWidget *parent = nullptr);
    ~CarPlayWidget() override;
    void setAndroidAutoActive(bool active);
    void setNightMode(bool night);
    void setAccentColor(const QColor &color);
    void setVolume(int percent) { client_->setVolume(percent); }
    void showSetup();
    void showAudioSettings();
    void setReceiverEnabled(bool enabled);
    bool receiverEnabled() const { return receiverEnabled_; }
    bool phoneActive() const { return receiverEnabled_ && !androidAutoActive_ && client_->sessionActive(); }
    void sendControl(PhoneControls::Command command, PhoneControls::State state) {
        if (phoneActive()) client_->sendControl(command, state);
    }
 signals:
    void managementRequested();
 private:
    bool androidAutoActive_ = false;
    bool receiverEnabled_ = true;
    bool restartPending_ = false;
    bool projectionReady_ = false;
    bool managing_ = false;
    void init();
    void updateTheme();
    CarPlayClient *client_ = nullptr;
    QProcess process_;
    QTimer poll_;
    QLabel *status_ = nullptr;
    QWidget *settings_ = nullptr;
    QLineEdit *ssid_ = nullptr;
    QLineEdit *password_ = nullptr;
    QLineEdit *identity_ = nullptr;
    QComboBox *phones_ = nullptr;
    QComboBox *resolution_ = nullptr;
    QComboBox *fps_ = nullptr;
    QSpinBox *width_ = nullptr;
    QSpinBox *height_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    QWidget *setup_ = nullptr;
    QWidget *surface_ = nullptr;
    QPushButton *resume_ = nullptr;
    QPushButton *start_ = nullptr;
    QPushButton *stop_ = nullptr;
    QPushButton *audioTab_ = nullptr;
    QString stateDirectory_;
    void startReceiver();
    void stopReceiver();
    void connectSelectedPhone();
    void savePhones();
    void addPhone(const QString &name, const QString &address);
    void saveDisplay();
};
#endif
