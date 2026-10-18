#pragma once

#include <array>
#include <functional>
#include <map>

namespace PhoneControls {
// Keep this order: Core/Action stores the original phone controls as IDs 0–16.
enum class Command {
    Left, Enter, Right, Up, Down, Back, Home, Phone, CallEnd,
    Play, Pause, Previous, Next, TogglePlay, Voice, ScrollUp, ScrollDown
};
enum class State { Pressed, Released, Triggered };
struct Binding { Command command; const char *name; };
const std::array<Binding, 17> &bindings();

class Router {
 public:
    using Active = std::function<bool()>;
    using Send = std::function<void(Command, State)>;
    Router(Active androidActive, Send androidSend, Active carplayActive, Send carplaySend);
    void dispatch(Command command, State state);
 private:
    enum class Target { None, AndroidAuto, CarPlay };
    Active androidActive_, carplayActive_;
    Send androidSend_, carplaySend_;
    std::map<Command, Target> held_;
    Target active() const;
    void send(Target target, Command command, State state);
};
}
