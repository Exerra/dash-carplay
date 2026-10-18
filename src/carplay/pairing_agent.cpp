#ifdef DASH_CARPLAY
#include "carplay/pairing_agent.hpp"
#include <QDBusConnection>

bool CarPlayPairingAgent::accepts(const QDBusObjectPath &device) const {
    auto suffix = selected_.toUpper(); suffix.replace(':', '_');
    return !suffix.isEmpty() && device.path().endsWith("/dev_" + suffix);
}
void CarPlayPairingAgent::reject() {
    if (calledFromDBus()) sendErrorReply("org.bluez.Error.Rejected", "Choose this iPhone in the Dash setup wizard first");
}
void CarPlayPairingAgent::confirm(bool accepted) {
    if (!waiting_) return;
    waiting_ = false;
    QDBusConnection::systemBus().send(accepted ? pending_.createReply()
        : pending_.createErrorReply("org.bluez.Error.Rejected", "Pairing declined"));
    pending_ = QDBusMessage();
}
void CarPlayPairingAgent::RequestConfirmation(const QDBusObjectPath &device, uint passkey) {
    if (!accepts(device) || waiting_) { reject(); return; }
    if (!calledFromDBus()) return;
    setDelayedReply(true); pending_ = message(); waiting_ = true;
    emit confirmation(QString::number(passkey).rightJustified(6, '0'));
}
void CarPlayPairingAgent::Cancel() { confirm(false); emit canceled(); }
void CarPlayPairingAgent::Release() { Cancel(); }
void CarPlayPairingAgent::RequestAuthorization(const QDBusObjectPath &device) { if (!accepts(device)) reject(); }
void CarPlayPairingAgent::AuthorizeService(const QDBusObjectPath &device, const QString &) { if (!accepts(device)) reject(); }
QString CarPlayPairingAgent::RequestPinCode(const QDBusObjectPath &) { reject(); return {}; }
uint CarPlayPairingAgent::RequestPasskey(const QDBusObjectPath &) { reject(); return 0; }
void CarPlayPairingAgent::DisplayPasskey(const QDBusObjectPath &device, uint code, ushort) {
    if (!accepts(device)) { reject(); return; }
    emit confirmation(QString::number(code).rightJustified(6, '0'));
}
void CarPlayPairingAgent::DisplayPinCode(const QDBusObjectPath &device, const QString &code) {
    if (!accepts(device)) { reject(); return; }
    emit confirmation(code);
}
#endif
