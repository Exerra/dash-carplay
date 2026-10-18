#pragma once
#ifdef DASH_CARPLAY
#include "carplay/client.hpp"
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>

class CarPlayAudioSettings : public QWidget {
    Q_OBJECT
 public:
    explicit CarPlayAudioSettings(CarPlayClient &client, QWidget *parent = nullptr);
 signals:
    void microphoneModeChanged();
 protected:
    void showEvent(QShowEvent *event) override;
 private:
    CarPlayClient &client_;
    QComboBox *output_ = nullptr, *latency_ = nullptr, *mode_ = nullptr, *input_ = nullptr;
    QSlider *music_ = nullptr, *voice_ = nullptr;
    QPushButton *refresh_ = nullptr;
    QLabel *status_ = nullptr;
    bool discovered_ = false, scanning_ = false;
    void refresh();
    void apply();
};
#endif
