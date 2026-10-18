#!/usr/bin/env python3
"""BlueZ iAP2 RFCOMM helper. stdout is binary transport; diagnostics go to stderr.

Implements the same --list/--print-address/--address interface as macos/bt-bridge.
Pair the phone with bluetoothctl first. No pairing agent or fixed RFCOMM channel
is installed: BlueZ discovers the iPhone's service using its iAP2 UUID.
"""

import argparse
import asyncio
import contextlib
import os
import re
import signal
import socket
import sys

IAP2_UUID = "00000000-deca-fade-deca-deafdecacafe"
PROFILE_PATH = "/com/playport/iap2"
ADAPTER = "org.bluez.Adapter1"
DEVICE = "org.bluez.Device1"


def normalize_address(value):
    value = value.replace("-", ":").upper()
    if not re.fullmatch(r"(?:[0-9A-F]{2}:){5}[0-9A-F]{2}", value):
        raise ValueError("Bluetooth address must have the form AA:BB:CC:DD:EE:FF")
    return value


def unwrap_objects(objects):
    return {path: {interface: {key: value.value for key, value in props.items()}
                   for interface, props in interfaces.items()}
            for path, interfaces in objects.items()}


def choose_adapter(objects, requested=None):
    adapters = [(path, interfaces[ADAPTER]) for path, interfaces in objects.items()
                if ADAPTER in interfaces]
    if requested:
        adapters = [(path, props) for path, props in adapters
                    if requested in (path, path.rsplit("/", 1)[-1])
                    or requested.upper() == props.get("Address", "").upper()]
    adapters = [(path, props) for path, props in adapters if props.get("Powered")]
    if not adapters:
        raise ValueError("No powered Bluetooth adapter found; check rfkill and bluetoothctl power on")
    if len(adapters) != 1:
        raise ValueError("Multiple powered Bluetooth adapters; set PLAYPORT_BT_ADAPTER=hci0 (or the desired adapter)")
    return adapters[0]


def paired_devices(objects, adapter_path):
    return sorted((path, interfaces[DEVICE]) for path, interfaces in objects.items()
                  if DEVICE in interfaces
                  and interfaces[DEVICE].get("Adapter") == adapter_path
                  and interfaces[DEVICE].get("Paired")
                  and not interfaces[DEVICE].get("Blocked", False))


def choose_device(objects, adapter_path, address):
    address = normalize_address(address)
    for path, props in paired_devices(objects, adapter_path):
        if props.get("Address", "").upper() == address:
            return path
    raise ValueError("iPhone is not paired on the selected adapter (or is blocked); pair it with bluetoothctl first")


async def fd_ready(fd, write=False):
    loop = asyncio.get_running_loop()
    ready = loop.create_future()

    def notify():
        if not ready.done():
            ready.set_result(None)

    add = loop.add_writer if write else loop.add_reader
    remove = loop.remove_writer if write else loop.remove_reader
    add(fd, notify)
    try:
        await ready
    finally:
        remove(fd)


async def pump_stdio(channel, input_fd=0, output_fd=1):
    """Bounded duplex pump; either EOF cancels the other reader without a thread leak."""
    loop = asyncio.get_running_loop()
    original = [(fd, os.get_blocking(fd)) for fd in (input_fd, output_fd)]
    channel.setblocking(False)
    for fd, _ in original:
        os.set_blocking(fd, False)

    async def to_phone():
        while True:
            await fd_ready(input_fd)
            try:
                data = os.read(input_fd, 65536)
            except BlockingIOError:
                continue
            if not data:
                return
            await loop.sock_sendall(channel, data)

    async def from_phone():
        while True:
            data = await loop.sock_recv(channel, 65536)
            if not data:
                return
            pending = memoryview(data)
            while pending:
                try:
                    written = os.write(output_fd, pending)
                    pending = pending[written:]
                except BlockingIOError:
                    await fd_ready(output_fd, write=True)

    tasks = [asyncio.create_task(to_phone()), asyncio.create_task(from_phone())]
    try:
        done, _ = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
        for task in done:
            task.result()
    finally:
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        for fd, blocking in original:
            os.set_blocking(fd, blocking)


def make_profile(device_path):
    # Lazy import keeps --help and the transport/selection tests independent of D-Bus.
    from dbus_next.errors import DBusError
    from dbus_next.service import ServiceInterface, method

    class Profile(ServiceInterface):
        def __init__(self):
            super().__init__("org.bluez.Profile1")
            self.connection = asyncio.get_running_loop().create_future()
            self.disconnected = asyncio.Event()

        @method()
        def Release(self):
            self.disconnected.set()

        @method()
        def NewConnection(self, device: 'o', fd: 'h', properties: 'a{sv}'):
            if device != device_path or self.connection.done():
                os.close(fd)
                raise DBusError("org.bluez.Error.Rejected", "Unexpected or duplicate connection")
            self.connection.set_result(socket.socket(fileno=fd))

        @method()
        def RequestDisconnection(self, device: 'o'):
            if device == device_path:
                self.disconnected.set()

    return Profile()


async def run(args):
    if sys.platform != "linux":
        raise ValueError("This helper requires Linux/BlueZ; use macos/bt-bridge on macOS")
    try:
        from dbus_next import BusType, Variant
        from dbus_next.aio import MessageBus
    except ImportError as error:
        raise ValueError("Install python3-dbus-next (see linux/README.md)") from error

    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    manager = None
    profile = None
    registered = False
    device = None
    channel = None
    connected = False
    try:
        root = bus.get_proxy_object("org.bluez", "/", await bus.introspect("org.bluez", "/"))
        objects = unwrap_objects(await root.get_interface("org.freedesktop.DBus.ObjectManager").call_get_managed_objects())
        adapter_path, adapter = choose_adapter(objects, args.adapter)
        if args.print_address:
            print(normalize_address(adapter["Address"]))
            return
        if args.list:
            for _, props in paired_devices(objects, adapter_path):
                name = " ".join(str(props.get("Name", props.get("Alias", "Unknown"))).split())
                print(normalize_address(props["Address"]), name)
            return

        device_path = choose_device(objects, adapter_path, args.address)
        profile = make_profile(device_path)
        bus.export(PROFILE_PATH, profile)
        bluez = bus.get_proxy_object("org.bluez", "/org/bluez", await bus.introspect("org.bluez", "/org/bluez"))
        manager = bluez.get_interface("org.bluez.ProfileManager1")
        await manager.call_register_profile(PROFILE_PATH, IAP2_UUID, {
            "Name": Variant("s", "PlayPort iAP2"),
            "Role": Variant("s", "client"),
            "RequireAuthentication": Variant("b", True),
            "RequireAuthorization": Variant("b", False),
            "AutoConnect": Variant("b", False),
        })
        registered = True
        proxy = bus.get_proxy_object("org.bluez", device_path, await bus.introspect("org.bluez", device_path))
        device = proxy.get_interface(DEVICE)
        await asyncio.wait_for(device.call_connect_profile(IAP2_UUID), args.timeout)
        connected = True
        channel = await asyncio.wait_for(asyncio.shield(profile.connection), args.timeout)
        print("bt-bridge: iAP2 RFCOMM connected", file=sys.stderr)
        pump = asyncio.create_task(pump_stdio(channel))
        disconnected = asyncio.create_task(profile.disconnected.wait())
        try:
            done, _ = await asyncio.wait([pump, disconnected], return_when=asyncio.FIRST_COMPLETED)
            for task in done:
                task.result()
        finally:
            pump.cancel()
            disconnected.cancel()
            await asyncio.gather(pump, disconnected, return_exceptions=True)
    finally:
        # A late NewConnection can arrive while ConnectProfile is being cancelled.
        if channel is None and profile and profile.connection.done() and not profile.connection.cancelled():
            channel = profile.connection.result()
        if channel:
            channel.close()
        if connected and device:
            with contextlib.suppress(Exception):
                await asyncio.wait_for(device.call_disconnect_profile(IAP2_UUID), 3)
        if registered:
            with contextlib.suppress(Exception):
                await asyncio.wait_for(manager.call_unregister_profile(PROFILE_PATH), 3)
        bus.disconnect()


async def entry(args):
    task = asyncio.current_task()
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, task.cancel)
    try:
        await run(args)
    except asyncio.CancelledError:
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--print-address", action="store_true")
    mode.add_argument("--list", action="store_true")
    mode.add_argument("--address", type=normalize_address)
    parser.add_argument("--adapter", default=os.environ.get("PLAYPORT_BT_ADAPTER"))
    parser.add_argument("--timeout", type=float, default=20)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    try:
        asyncio.run(entry(args))
    except Exception as error:
        print(f"bt-bridge: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
