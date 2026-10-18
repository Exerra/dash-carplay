# Linux Bluetooth bridge

This is the BlueZ counterpart to the upstream macOS helper. It registers a client
profile for the iPhone's iAP2 service and asks BlueZ to discover/connect RFCOMM.
It does not hard-code a channel, install a pairing agent, or modify pairing state.
The phone must already be paired with the selected adapter.

From `dash/carplay/receiver` on the Pi:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r linux/requirements.txt
linux/bt-bridge --print-address
linux/bt-bridge --list
```

Python 3.9+ and the running BlueZ system service are required. The helper automatically
uses `.venv/bin/python` when present. `PLAYPORT_PYTHON` can select another interpreter.
For multiple adapters, select one with `PLAYPORT_BT_ADAPTER=hci0` (or its MAC address).

Pair using the system's Bluetooth settings or an interactive `bluetoothctl` session:
power on, enable an agent, scan, then pair the desired device and approve the code
on the iPhone. Do not start the bridge as root to work around pairing problems.
If D-Bus rejects profile registration, check the system's BlueZ/D-Bus policy for
the account running Dash; exact permissions depend on the installed OS.

For a manual transport check after pairing:

```sh
linux/bt-bridge --address AA:BB:CC:DD:EE:FF
```

That last command opens a binary channel; it is normally launched by the receiver,
not used interactively. All its diagnostics go to stderr. The registered profile
is removed when the helper exits. Closing it disconnects only the iAP2 profile,
not the phone's other Bluetooth profiles.

Local tests (no Bluetooth required):

```sh
.venv/bin/python -m unittest discover -s linux -v
```

Tests exercise duplex binary transport, backpressure, cancellation, file-descriptor
ownership and device selection. A real BlueZ/iPhone connection is still unverified.
API reference: https://github.com/bluez/bluez/blob/master/doc/org.bluez.ProfileManager.rst
