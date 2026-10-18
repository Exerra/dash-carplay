#!/usr/bin/env python3
"""Read-only checks for the experimental Dash CarPlay receiver on a target Pi."""
import argparse
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys


def command(*args, timeout=12):
    try:
        result = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
        return result.returncode == 0, (result.stdout + result.stderr).strip()
    except (OSError, subprocess.TimeoutExpired) as error:
        return False, str(error)


def main():
    root = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--identity-dir", type=Path,
                        default=Path(os.environ.get("CARPLAY_IDENTITY_DIR", root / "runtime/identity")))
    args = parser.parse_args()
    failures = []

    def check(name, ok, detail):
        print(f"{'OK' if ok else 'NEEDS ATTENTION'}: {name}: {detail}")
        if not ok:
            failures.append(name)

    print("Dash CarPlay preflight (does not change the system)")
    check("Linux", platform.system() == "Linux", f"{platform.system()} {platform.machine()}")
    java = str(Path(os.environ["JAVA_HOME"]) / "bin/java") if os.environ.get("JAVA_HOME") else "java"
    ok, output = command(java, "-version")
    version = re.search(r'version "(\d+)', output)
    check("Java 21+", ok and version is not None and int(version[1]) >= 21,
          output.splitlines()[0] if output else "Java not found")
    identity = args.identity_dir / "offline-mfi"
    check("Runtime identity", all((identity / name).is_file() and (identity / name).stat().st_size > 0
          for name in ("identity.pk8", "certificate.p7b")), str(identity))
    check("Receiver build", (root / "receiver/server/build/install/server/bin/server").is_file(),
          "run carplay/build.sh if missing")
    if platform.system() == "Linux":
        ok, output = command(str(root / "receiver/linux/bt-bridge"), "--print-address")
        check("Bluetooth adapter", ok, output)
        ok, output = command(str(root / "receiver/linux/bt-bridge"), "--list")
        check("Paired Bluetooth devices", ok and bool(output), output or "Pair the iPhone first")
        ok, output = command("ip", "-j", "address", "show", "up")
        addresses = []
        if ok:
            try:
                for interface in json.loads(output):
                    if interface.get("ifname") == "lo": continue
                    addresses.extend(f'{interface["ifname"]}: {a["local"]}'
                                     for a in interface.get("addr_info", []) if a.get("family") == "inet")
            except (ValueError, KeyError):
                pass
        check("Network address", bool(addresses), "; ".join(addresses) or "Join the same Wi-Fi LAN as the iPhone")
        for element in ("appsrc", "appsink", "h264parse", "decodebin", "videoconvert", "autoaudiosink"):
            ok, _ = command("gst-inspect-1.0", element)
            check(f"GStreamer {element}", ok, "available" if ok else "install the corresponding GStreamer plugin")
    print("Phone authentication, actual audio/video and touch require an on-device test.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
