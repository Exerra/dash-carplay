#pragma once
#ifdef DASH_CARPLAY
#include <QObject>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>

// BlueZ 5 uses an Agent1 on the pairing caller's bus connection.
class CarPlayPairingAgent : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1")
 public:
    explicit CarPlayPairingAgent(QObject *parent = nullptr) : QObject(parent) {}
    void select(const QString &address) { selected_ = address; }
    void confirm(bool accepted);
    bool accepts(const QDBusObjectPath &device) const;
 signals:
    void confirmation(const QString &code);
    void canceled();
 public slots:
    void Release();
    void Cancel();
    void RequestConfirmation(const QDBusObjectPath &device, uint passkey);
    void RequestAuthorization(const QDBusObjectPath &device);
    void AuthorizeService(const QDBusObjectPath &device, const QString &uuid);
    QString RequestPinCode(const QDBusObjectPath &device);
    uint RequestPasskey(const QDBusObjectPath &device);
    void DisplayPasskey(const QDBusObjectPath &device, uint passkey, ushort entered);
    void DisplayPinCode(const QDBusObjectPath &device, const QString &pincode);
 private:
    QString selected_;
    QDBusMessage pending_;
    bool waiting_ = false;
    void reject();
};
#endif
