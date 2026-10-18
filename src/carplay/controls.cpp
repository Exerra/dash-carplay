#ifdef DASH_CARPLAY
#include "carplay/client.hpp"

void CarPlayClient::sendControl(PhoneControls::Command command, PhoneControls::State state) {
    using Command = PhoneControls::Command;
    using State = PhoneControls::State;
    if (!sessionActive_) return;
    const bool triggered = state == State::Triggered, down = state != State::Released;
    QJsonObject message;
    const bool wheel = command == Command::ScrollUp || command == Command::ScrollDown;
    if (wheel || command <= Command::Home) {
        if (wheel && !down) return; // Relative wheel movement is sent only once per click.
        if (!wheel) {
            if (down) heldControls_.insert(command); else heldControls_.erase(command);
        }
        const auto held = [this](Command key) { return heldControls_.count(key) != 0; };
        const QJsonObject knob{{"x", (held(Command::Right) - held(Command::Left)) * 127},
            {"y", (held(Command::Down) - held(Command::Up)) * 127},
            {"select", held(Command::Enter)}, {"home", held(Command::Home)}, {"back", held(Command::Back)},
            {"wheel", wheel ? (command == Command::ScrollUp ? 1 : -1) : 0}};
        message = {{"type", "knob"}, {"knob", knob}};
        // All native knob states are explicit; a tap releases just its own button.
        message.insert("pressed", down);
        sendCommand(message);
        if (triggered && !wheel) sendControl(command, State::Released);
        return;
    } else {
        switch (command) {
            case Command::Phone: message = {{"type", "telephony"}, {"telephony", 1}}; break;
            case Command::CallEnd: message = {{"type", "telephony"}, {"telephony", 3}}; break;
            case Command::Play: message = {{"type", "media"}, {"media", 1}}; break;
            case Command::Pause: message = {{"type", "media"}, {"media", 2}}; break;
            case Command::Previous: message = {{"type", "media"}, {"media", 5}}; break;
            case Command::Next: message = {{"type", "media"}, {"media", 4}}; break;
            case Command::TogglePlay: message = {{"type", "media"}, {"media", 3}}; break;
            case Command::Voice: message = {{"type", "siri"}}; break;
            default: return;
        }
        if (!triggered) message.insert("pressed", down);
    }
    sendCommand(message);
}
#endif
