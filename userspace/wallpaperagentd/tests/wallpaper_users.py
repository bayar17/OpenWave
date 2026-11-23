#!/usr/bin/env python3
"""Check per-user wallpaper storage and the integrity fallback."""

import hashlib
import io
import os
from pathlib import Path
import subprocess
import tempfile
import time

import gi
from PIL import Image

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

ROOT = Path(__file__).resolve().parents[3]
BUS = "org.openwave.Wallpaper"
PATH = "/org/openwave/Wallpaper"
IFACE = "org.openwave.Wallpaper1"


def call(bus, method, parameters, reply_type):
    return bus.call_sync(BUS, PATH, IFACE, method, parameters,
        GLib.VariantType.new(reply_type), Gio.DBusCallFlags.NONE, 5000, None)


def main():
    os.environ["DBUS_SYSTEM_BUS_ADDRESS"] = os.environ["DBUS_SESSION_BUS_ADDRESS"]
    with tempfile.TemporaryDirectory(prefix="openwave-wallpaper-user-test-") as directory:
        loader = ROOT / "fakeroot/lib64/ld-linux-x86-64.so.2"
        binary = ROOT / "out/openwave-userspace/wallpaperagentd/wallpaperagentd"
        service = subprocess.Popen([
            str(loader), "--library-path",
            f"{ROOT}/fakeroot/usr/lib64:{ROOT}/fakeroot/lib64",
            str(binary), "--root", directory,
        ], env=os.environ.copy())
        try:
            bus = Gio.DBusConnection.new_for_address_sync(
                os.environ["DBUS_SESSION_BUS_ADDRESS"],
                Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT
                | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION, None, None)
            for _ in range(50):
                try:
                    assert call(bus, "GetCurrent", None, "(ss)").unpack() == ("", "")
                    break
                except GLib.Error as error:
                    if "ServiceUnknown" not in error.message:
                        raise
                    time.sleep(0.1)
            else:
                raise AssertionError("Wallpaper agent did not start")
            output = io.BytesIO()
            Image.new("RGB", (768, 1366), (25, 80, 120)).save(output, "PNG")
            png = output.getvalue()
            digest = hashlib.sha256(png).hexdigest()
            assert call(bus, "SetWallpaper", GLib.Variant("(ay)", (png,)), "(s)")[0] == digest
            public_path = (f"/var/openwave/wallpaper/users/{os.getuid()}/{digest}.png"
                           if os.getuid() else f"/var/openwave/wallpaper/{digest}.png")
            assert call(bus, "GetCurrent", None, "(ss)").unpack() == (public_path, digest)
            physical = Path(directory) / public_path.lstrip("/")
            assert physical.read_bytes() == png
            physical.write_bytes(png + b"tampered")
            assert call(bus, "GetCurrent", None, "(ss)").unpack() == ("", "")
            print("wallpaper user storage and SHA-256 fallback passed")
        finally:
            service.terminate()
            service.wait(timeout=5)


if __name__ == "__main__":
    main()
