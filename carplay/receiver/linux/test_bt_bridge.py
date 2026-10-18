import asyncio
import os
import socket
import unittest

from bt_bridge import (ADAPTER, DEVICE, choose_adapter, choose_device, make_profile,
                       normalize_address, paired_devices, pump_stdio)


class DeviceSelectionTest(unittest.TestCase):
    def setUp(self):
        self.objects = {
            "/org/bluez/hci0": {ADAPTER: {"Address": "00:11:22:33:44:55", "Powered": True}},
            "/org/bluez/hci1": {ADAPTER: {"Address": "00:11:22:33:44:66", "Powered": False}},
            "/phone": {DEVICE: {"Adapter": "/org/bluez/hci0", "Address": "AA:BB:CC:DD:EE:FF", "Paired": True}},
            "/other": {DEVICE: {"Adapter": "/org/bluez/hci1", "Address": "AA:BB:CC:DD:EE:FF", "Paired": True}},
        }

    def test_adapter_and_device_are_selected_together(self):
        path, props = choose_adapter(self.objects)
        self.assertEqual(path, "/org/bluez/hci0")
        self.assertEqual(choose_device(self.objects, path, "aa-bb-cc-dd-ee-ff"), "/phone")
        self.assertEqual(len(paired_devices(self.objects, path)), 1)
        self.assertEqual(choose_adapter(self.objects, props["Address"])[0], path)

    def test_ambiguous_or_unpowered_adapter_requires_a_choice(self):
        with self.assertRaises(ValueError):
            choose_adapter(self.objects, "hci1")
        self.objects["/org/bluez/hci1"][ADAPTER]["Powered"] = True
        with self.assertRaises(ValueError):
            choose_adapter(self.objects)
        self.assertEqual(choose_adapter(self.objects, "hci1")[0], "/org/bluez/hci1")

    def test_blocked_or_unpaired_device_cannot_connect(self):
        self.objects["/phone"][DEVICE]["Blocked"] = True
        with self.assertRaises(ValueError):
            choose_device(self.objects, "/org/bluez/hci0", "AA:BB:CC:DD:EE:FF")
        for bad in ("", "AA:BB", "AA:BB:CC:DD:EE:FF\n", "GG:BB:CC:DD:EE:FF"):
            with self.assertRaises(ValueError):
                normalize_address(bad)


class BridgeTransportTest(unittest.IsolatedAsyncioTestCase):
    async def test_binary_duplex_preserves_order_under_pipe_backpressure(self):
        channel, peer = socket.socketpair()
        input_read, input_write = os.pipe()
        output_read, output_write = os.pipe()
        pump = asyncio.create_task(pump_stdio(channel, input_read, output_write))
        peer.setblocking(False)
        loop = asyncio.get_running_loop()
        outbound = bytes(range(256)) * 400
        inbound = bytes(reversed(range(256))) * 400

        def write_all():
            remaining = memoryview(outbound)
            while remaining:
                remaining = remaining[os.write(input_write, remaining):]

        def read_all():
            result = bytearray()
            while len(result) < len(inbound):
                result.extend(os.read(output_read, 8192))
            return bytes(result)

        async def receive():
            result = bytearray()
            while len(result) < len(outbound):
                result.extend(await loop.sock_recv(peer, 113))
            return bytes(result)

        try:
            results = await asyncio.wait_for(asyncio.gather(
                asyncio.to_thread(write_all), loop.sock_sendall(peer, inbound),
                receive(), asyncio.to_thread(read_all)), 5)
            self.assertEqual(results[2], outbound)
            self.assertEqual(results[3], inbound)
            peer.close()
            await asyncio.wait_for(pump, 1)
        finally:
            pump.cancel()
            await asyncio.gather(pump, return_exceptions=True)
            channel.close()
            peer.close()
            for fd in (input_read, input_write, output_read, output_write):
                os.close(fd)

    async def test_cancellation_does_not_leave_a_blocked_stdin_thread(self):
        channel, peer = socket.socketpair()
        ir, iw = os.pipe()
        or_, ow = os.pipe()
        task = asyncio.create_task(pump_stdio(channel, ir, ow))
        try:
            await asyncio.sleep(.01)
            task.cancel()
            await asyncio.wait_for(asyncio.gather(task, return_exceptions=True), 1)
            self.assertTrue(os.get_blocking(ir))
            self.assertTrue(os.get_blocking(ow))
        finally:
            channel.close()
            peer.close()
            for fd in (ir, iw, or_, ow):
                os.close(fd)

    async def test_profile_accepts_only_one_fd_from_the_selected_phone(self):
        from dbus_next.errors import DBusError
        profile = make_profile("/phone")
        first, second = socket.socketpair()
        accepted = os.dup(first.fileno())
        try:
            profile.NewConnection("/phone", accepted, {})
            connection = await profile.connection
            with self.assertRaises(DBusError):
                profile.NewConnection("/other", os.dup(second.fileno()), {})
            profile.RequestDisconnection("/phone")
            self.assertTrue(profile.disconnected.is_set())
            connection.close()
        finally:
            first.close()
            second.close()


if __name__ == "__main__":
    unittest.main()
