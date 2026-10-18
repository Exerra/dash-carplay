#pragma once

#include <QWidget>
#include <functional>
class QCheckBox;
class QLabel;
class QPushButton;

namespace Receivers {
constexpr int RestartExitCode = 75;
bool androidAutoEnabled();
bool carplayEnabled();
}

class ReceiverSettings : public QWidget {
    Q_OBJECT
 public:
    ReceiverSettings(bool runningAndroidAuto, std::function<void(bool)> setCarplay, QWidget *parent = nullptr);
 signals:
    void restartRequested();
 private:
    bool runningAndroidAuto_;
    std::function<void(bool)> setCarplay_;
    QCheckBox *androidAuto_;
    QCheckBox *carplay_ = nullptr;
    QLabel *status_;
    QPushButton *apply_;
    void updateAction();
    void apply();
};
