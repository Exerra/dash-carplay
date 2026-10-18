#pragma once
#ifdef DASH_CARPLAY
#include <QObject>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothLocalDevice>
#include <QTimer>
#include "carplay/pairing_agent.hpp"

// Bluetooth discovery/pairing lives only while the setup wizard is open.
class CarPlayPhones : public QObject {
    Q_OBJECT
 public:
    explicit CarPlayPhones(QObject *parent = nullptr, bool useSystemBluetooth = true);
    ~CarPlayPhones() override;
    void scan();
    void pair(const QString &address);
    void confirm(bool accepted);
    QString adapterName() const;
 signals:
    void found(const QString &name, const QString &address, bool paired);
    void paired(const QString &address);
    void confirmation(const QString &code);
    void status(const QString &message);
 private:
    QBluetoothLocalDevice *local_ = nullptr;
    QBluetoothDeviceDiscoveryAgent *discovery_ = nullptr;
    QBluetoothLocalDevice::HostMode previousMode_ = QBluetoothLocalDevice::HostConnectable;
    QTimer timeout_;
    QString pending_;
    CarPlayPairingAgent *agent_ = nullptr;
    bool agentRegistered_ = false;
    QHash<QString, QString> devicePaths_;
    QString adapterPath_;
    void knownDevices();
    void cancelPairing();
};
#endif
