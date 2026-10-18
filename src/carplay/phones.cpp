#ifdef DASH_CARPLAY
#include "carplay/phones.hpp"
#include <QBluetoothAddress>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

namespace { const QString agentPath = "/org/opendash/CarPlayPairing"; }

CarPlayPhones::CarPlayPhones(QObject *parent, bool useSystemBluetooth) : QObject(parent) {
#ifdef Q_OS_LINUX
    if (useSystemBluetooth) {
    local_ = new QBluetoothLocalDevice(this);
    previousMode_ = local_->hostMode() == QBluetoothLocalDevice::HostPoweredOff
        ? QBluetoothLocalDevice::HostConnectable : local_->hostMode();
    discovery_ = new QBluetoothDeviceDiscoveryAgent(local_->address(), this);
    agent_ = new CarPlayPairingAgent(this);
    QDBusConnection::systemBus().registerObject(agentPath, agent_, QDBusConnection::ExportAllSlots);
    connect(agent_, &CarPlayPairingAgent::confirmation, this, &CarPlayPhones::confirmation);
    connect(agent_, &CarPlayPairingAgent::canceled, this, [this] {
        timeout_.stop(); pending_.clear(); emit status("Pairing canceled. Choose your iPhone to try again.");
    });
    connect(discovery_, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, [this](const QBluetoothDeviceInfo &device) {
        auto suffix = device.address().toString(); suffix.replace(':', '_');
        if (!adapterPath_.isEmpty()) devicePaths_.insert(device.address().toString(), adapterPath_ + "/dev_" + suffix);
        emit found(device.name(), device.address().toString(), local_->pairingStatus(device.address()) != QBluetoothLocalDevice::Unpaired);
    });
    connect(discovery_, QOverload<QBluetoothDeviceDiscoveryAgent::Error>::of(&QBluetoothDeviceDiscoveryAgent::error),
        this, [this] { emit status("Bluetooth search failed. Check that Bluetooth is switched on, then search again."); });
    connect(discovery_, &QBluetoothDeviceDiscoveryAgent::finished, this, [this] {
        emit status("Choose your iPhone. If it is missing, leave its Bluetooth settings open and search again.");
    });
    connect(local_, &QBluetoothLocalDevice::pairingFinished, this, [this](const QBluetoothAddress &address, QBluetoothLocalDevice::Pairing state) {
        if (address.toString() != pending_ || state == QBluetoothLocalDevice::Unpaired) return;
        timeout_.stop(); pending_.clear(); emit paired(address.toString());
    });
    connect(local_, &QBluetoothLocalDevice::pairingDisplayConfirmation, this, [this](const QBluetoothAddress &, const QString &code) {
        emit confirmation(code);
    });
    connect(local_, &QBluetoothLocalDevice::pairingDisplayPinCode, this, [this](const QBluetoothAddress &, const QString &code) {
        emit status("Enter this code on your iPhone: " + code);
    });
    connect(local_, &QBluetoothLocalDevice::error, this, [this] {
        timeout_.stop(); pending_.clear(); emit status("Pairing did not finish. Keep the iPhone's Bluetooth settings open and try again.");
    });
    }
#else
    Q_UNUSED(useSystemBluetooth)
#endif
    timeout_.setSingleShot(true);
    connect(&timeout_, &QTimer::timeout, this, [this] {
        cancelPairing(); emit status("Pairing timed out. Approve the request on the iPhone and try again.");
    });
}

CarPlayPhones::~CarPlayPhones() {
    cancelPairing();
    if (agent_) {
        auto message = QDBusMessage::createMethodCall("org.bluez", "/org/bluez", "org.bluez.AgentManager1", "UnregisterAgent");
        message << QVariant::fromValue(QDBusObjectPath(agentPath));
        QDBusConnection::systemBus().asyncCall(message);
        QDBusConnection::systemBus().unregisterObject(agentPath);
    }
    if (discovery_) discovery_->stop();
    if (local_ && local_->isValid() && local_->hostMode() != previousMode_)
        local_->setHostMode(previousMode_);
}

QString CarPlayPhones::adapterName() const { return local_ ? local_->name() : "OpenDash"; }

void CarPlayPhones::scan() {
    if (!local_ || !local_->isValid()) { emit status("Phone pairing is available on the Raspberry Pi with Bluetooth enabled."); return; }
    local_->powerOn();
    local_->setHostMode(QBluetoothLocalDevice::HostDiscoverable);
    knownDevices();
    if (!discovery_->isActive()) discovery_->start(QBluetoothDeviceDiscoveryAgent::ClassicMethod);
    emit status("Searching… Open Settings → Bluetooth on your iPhone.");
}

void CarPlayPhones::knownDevices() {
    auto message = QDBusMessage::createMethodCall("org.bluez", "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        const auto reply = watcher->reply(); watcher->deleteLater();
        if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) return;
        const auto objects = qvariant_cast<QDBusArgument>(reply.arguments().first());
        objects.beginMap();
        while (!objects.atEnd()) {
            QDBusObjectPath path;
            QMap<QString, QVariantMap> interfaces;
            objects.beginMapEntry(); objects >> path >> interfaces; objects.endMapEntry();
            if (interfaces.value("org.bluez.Adapter1").value("Address").toString().compare(local_->address().toString(), Qt::CaseInsensitive) == 0)
                adapterPath_ = path.path();
            const auto properties = interfaces.value("org.bluez.Device1");
            const auto address = properties.value("Address").toString();
            if (!address.isEmpty()) devicePaths_.insert(address.toUpper(), path.path());
            if (!address.isEmpty() && properties.value("Paired").toBool() && !properties.value("Blocked").toBool() &&
                local_->pairingStatus(QBluetoothAddress(address)) != QBluetoothLocalDevice::Unpaired)
                emit found(properties.value("Alias", properties.value("Name")).toString(), address, true);
        }
        objects.endMap();
    });
}

void CarPlayPhones::pair(const QString &address) {
    if (!local_ || QBluetoothAddress(address).isNull()) return;
    if (!pending_.isEmpty()) { emit status("Approve the existing pairing request on your iPhone first."); return; }
    discovery_->stop();
    pending_ = QBluetoothAddress(address).toString();
    if (local_->pairingStatus(QBluetoothAddress(address)) != QBluetoothLocalDevice::Unpaired) {
        const auto known = pending_; pending_.clear(); QTimer::singleShot(0, this, [this, known] { emit paired(known); }); return;
    }
    emit status("Approve the pairing request on your iPhone.");
    timeout_.start(45000);
    agent_->select(pending_);
    if (agentRegistered_) { local_->requestPairing(QBluetoothAddress(address), QBluetoothLocalDevice::AuthorizedPaired); return; }
    auto message = QDBusMessage::createMethodCall("org.bluez", "/org/bluez", "org.bluez.AgentManager1", "RegisterAgent");
    message << QVariant::fromValue(QDBusObjectPath(agentPath)) << "DisplayYesNo";
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        const auto reply = watcher->reply(); watcher->deleteLater();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            timeout_.stop(); pending_.clear(); emit status("Could not start Bluetooth pairing. Close other pairing dialogs and try again."); return;
        }
        agentRegistered_ = true;
        if (!pending_.isEmpty()) local_->requestPairing(QBluetoothAddress(pending_), QBluetoothLocalDevice::AuthorizedPaired);
    });
}

void CarPlayPhones::confirm(bool accepted) { if (agent_) agent_->confirm(accepted); }

void CarPlayPhones::cancelPairing() {
    if (agent_) agent_->confirm(false);
    const auto path = devicePaths_.value(pending_);
    if (!path.isEmpty())
        QDBusConnection::systemBus().asyncCall(QDBusMessage::createMethodCall("org.bluez", path, "org.bluez.Device1", "CancelPairing"));
    pending_.clear();
}
#endif
