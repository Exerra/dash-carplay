#pragma once
#ifdef DASH_CARPLAY
#include <QWizard>
#include <QListWidget>
#include <QLabel>
#include <QLineEdit>
#include "carplay/phones.hpp"

class CarPlayPhoneWizard : public QWizard {
 public:
    // An injected controller is adopted by the wizard (used by offline UI tests).
    CarPlayPhoneWizard(const QString &ssid, const QString &password, QWidget *parent = nullptr, CarPlayPhones *bluetooth = nullptr);
    QString phoneName() const;
    QString phoneAddress() const;
    QString ssid() const;
    QString password() const;
 protected:
    bool validateCurrentPage() override;
 private:
    CarPlayPhones *bluetooth_;
    QListWidget *devices_;
    QLabel *status_;
    QLineEdit *ssid_;
    QLineEdit *password_;
    QLineEdit *name_;
};
#endif
