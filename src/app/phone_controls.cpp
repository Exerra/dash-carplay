#include "app/phone_controls.hpp"
#include <utility>

namespace PhoneControls {
const std::array<Binding, 17> &bindings() {
    static const std::array<Binding, 17> values = {{
        {Command::Left, "Phone Left"}, {Command::Enter, "Phone Enter"},
        {Command::Right, "Phone Right"}, {Command::Up, "Phone Up"},
        {Command::Down, "Phone Down"}, {Command::Back, "Phone Back"},
        {Command::Home, "Phone Home"}, {Command::Phone, "Phone / Answer Call"},
        {Command::CallEnd, "Phone Call End"}, {Command::Play, "Phone Play"},
        {Command::Pause, "Phone Pause"}, {Command::Previous, "Phone Prev Track"},
        {Command::Next, "Phone Next Track"}, {Command::TogglePlay, "Phone Toggle Play"},
        {Command::Voice, "Phone Voice"}, {Command::ScrollUp, "Phone Scroll Up"},
        {Command::ScrollDown, "Phone Scroll Down"}
    }};
    return values;
}

Router::Router(Active androidActive, Send androidSend, Active carplayActive, Send carplaySend)
    : androidActive_(std::move(androidActive)), carplayActive_(std::move(carplayActive)),
      androidSend_(std::move(androidSend)), carplaySend_(std::move(carplaySend)) { }

Router::Target Router::active() const {
    if (androidActive_()) return Target::AndroidAuto;
    if (carplayActive_()) return Target::CarPlay;
    return Target::None;
}

void Router::send(Target target, Command command, State state) {
    if (target == Target::AndroidAuto && androidActive_()) androidSend_(command, state);
    else if (target == Target::CarPlay && carplayActive_()) carplaySend_(command, state);
}

void Router::dispatch(Command command, State state) {
    if (state == State::Triggered) { send(active(), command, state); return; }
    if (state == State::Pressed) {
        if (held_.count(command)) return; // One down/up pair even if a key repeats.
        const auto target = active(); held_[command] = target;
        send(target, command, state);
    } else {
        const auto found = held_.find(command);
        if (found == held_.end()) return;
        const auto target = found->second; held_.erase(found);
        // Keep the release with the original backend after a connection switch.
        send(target, command, state);
    }
}
}
