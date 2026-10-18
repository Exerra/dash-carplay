#pragma once
#ifdef DASH_CARPLAY
#include "app/pages/page.hpp"
#include "app/phone_controls.hpp"
#include <QWidget>
class CarPlayWidget;
class CarPlayPage : public QWidget, public Page {
    Q_OBJECT
 public:
    explicit CarPlayPage(Arbiter &arbiter, QWidget *parent = nullptr);
    void init() override;
    bool phoneActive() const;
    void sendControl(PhoneControls::Command command, PhoneControls::State state);
 private:
    CarPlayWidget *widget_ = nullptr;
};
#endif
