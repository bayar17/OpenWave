#!/usr/bin/env python3
"""Exercise the appearance methods against a daemon running on a temporary root."""

import hashlib
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

import gi
from PIL import Image

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

BUS = "org.openwave.Wallpaper"
PATH = "/org/openwave/Wallpaper"
IFACE = "org.openwave.Wallpaper1"


def call(bus, method, parameters, reply_type):
    return bus.call_sync(BUS, PATH, IFACE, method, parameters,
        GLib.VariantType.new(reply_type), Gio.DBusCallFlags.NONE, 5000, None)


def png_bytes(size, color):
    output = io.BytesIO()
    Image.new("RGB", size, color).save(output, "PNG")
    return output.getvalue()


def expect_error(bus, method, parameters, fragment):
    try:
        call(bus, method, parameters, "(s)")
    except GLib.Error as error:
        assert fragment in error.message, error.message
        return
    raise AssertionError(f"{method} unexpectedly succeeded")


def main():
    launcher = sys.argv[1]
    loader = sys.argv[2]
    library_path = sys.argv[3]
    os.environ["DBUS_SYSTEM_BUS_ADDRESS"] = os.environ["DBUS_SESSION_BUS_ADDRESS"]
    with tempfile.TemporaryDirectory(prefix="openwave-appearance-test-") as directory:
        root = Path(directory)
        appearances = root / "System/QuickMenu/Appearances"
        appearances.mkdir(parents=True)
        good = png_bytes((768, 1366), (200, 120, 60))
        (appearances / "dusk.png").write_bytes(good)
        (appearances / "dusk.desktop").write_text(
            "[Desktop Entry]\nType=X-OpenWave-Appearance\nName=Dusk\n"
            "X-OpenWave-Appearance-Wallpaper=dusk.png\n")
        (appearances / "nowall.desktop").write_text(
            "[Desktop Entry]\nType=X-OpenWave-Appearance\nName=None\n")
        (appearances / "corrupt.png").write_bytes(b"not a png at all, just bytes" * 4)
        (appearances / "corrupt.desktop").write_text(
            "[Desktop Entry]\nType=X-OpenWave-Appearance\nX-OpenWave-Appearance-Wallpaper=corrupt.png\n")
        service = subprocess.Popen([loader, "--library-path", library_path, launcher, "--root", directory],
                                   env=os.environ.copy())
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
            digest = hashlib.sha256(good).hexdigest()
            assert call(bus, "ApplyAppearance", GLib.Variant("(s)", ("dusk",)), "(s)")[0] == digest
            public_path = (f"/var/openwave/wallpaper/users/{os.getuid()}/{digest}.png"
                           if os.getuid() else f"/var/openwave/wallpaper/{digest}.png")
            assert call(bus, "GetCurrent", None, "(ss)").unpack() == (public_path, digest)
            assert (root / public_path.lstrip("/")).read_bytes() == good
            for identifier in ("missing", "corrupt", "../dusk", "Dusk", "", "du/sk"):
                expect_error(bus, "ApplyAppearance", GLib.Variant("(s)", (identifier,)), "")
            assert call(bus, "GetCurrent", None, "(ss)").unpack() == (public_path, digest)
            uid = os.getuid()
            store = root / f"var/openwave/appearances/users/{uid}"
            assert (store / "current").read_text() == "dusk"
            assert call(bus, "ApplyAppearance", GLib.Variant("(s)", ("nowall",)), "(s)")[0] == ""
            assert (store / "current").read_text() == "nowall"
            assert call(bus, "GetCurrent", None, "(ss)").unpack() == (public_path, digest)
            assert call(bus, "ApplyAppearance", GLib.Variant("(s)", ("dusk",)), "(s)")[0] == digest
            assert (store / "current").read_text() == "dusk"
            expect_error(bus, "ApplyAppearance", GLib.Variant("(s)", ("missing",)), "not installed")
            expect_error(bus, "ApplyAppearance", GLib.Variant("(s)", ("corrupt",)), "must be a PNG or JPEG")
            assert (store / "current").read_text() == "dusk"

            def save(identifier, name, hue, data):
                return call(bus, "SaveAppearance", GLib.Variant("(ssday)", (identifier, name, hue, data)), "(s)")[0]

            def delete(identifier):
                bus.call_sync(BUS, PATH, IFACE, "DeleteAppearance", GLib.Variant("(s)", (identifier,)),
                              None, Gio.DBusCallFlags.NONE, 5000, None)

            sunset = png_bytes((768, 1366), (240, 160, 90))
            created = save("", "My Sunset", 0.1, sunset)
            assert created == "my-sunset", created
            text = (store / "my-sunset.desktop").read_text()
            assert "Type=X-OpenWave-Appearance" in text and "Name=My Sunset" in text, text
            assert "X-OpenWave-Appearance-Hue=0.1000" in text and "X-OpenWave-Appearance-Order=1010" in text, text
            assert "X-OpenWave-Appearance-Wallpaper=my-sunset-wallpaper.png" in text, text
            assert (store / "my-sunset-wallpaper.png").read_bytes() == sunset
            assert oct((store / "my-sunset.desktop").stat().st_mode & 0o777) == "0o644"
            sunset_digest = hashlib.sha256(sunset).hexdigest()
            assert call(bus, "ApplyAppearance", GLib.Variant("(s)", ("my-sunset",)), "(s)")[0] == sunset_digest

            assert save("", "My Sunset", 0.2, sunset) == "my-sunset-2"
            assert save("", "Dusk", 0.3, sunset) == "dusk-2"
            assert save("", "  !!  ", 0.3, sunset) == "a"
            assert save("", "\u00dcber Wave", 0.3, sunset).startswith("ber-wave")
            assert save("", "Back\\slash", 0.3, sunset) == "back-slash"
            assert "Name=Back\\\\slash" in (store / "back-slash.desktop").read_text()

            kept = save("my-sunset", "Renamed", 0.5, b"")
            assert kept == "my-sunset"
            text = (store / "my-sunset.desktop").read_text()
            assert "Name=Renamed" in text and "X-OpenWave-Appearance-Hue=0.5000" in text, text
            assert "X-OpenWave-Appearance-Order=1010" in text, text
            assert (store / "my-sunset-wallpaper.png").read_bytes() == sunset
            replacement = png_bytes((768, 1366), (10, 20, 30))
            assert save("my-sunset", "Renamed", 0.5, replacement) == "my-sunset"
            assert (store / "my-sunset-wallpaper.png").read_bytes() == replacement
            assert not list(store.glob("*.tmp.*"))

            for name, hue, data in (("", 0.1, sunset), ("x" * 33, 0.1, sunset), ("bad\nname", 0.1, sunset),
                                    ("Hue", 2.0, sunset), ("Hue", -0.1, sunset),
                                    ("Corrupt", 0.1, b"nope" * 20), ("NoImage", 0.1, b"")):
                expect_error(bus, "SaveAppearance", GLib.Variant("(ssday)", ("", name, hue, data)), "")
            expect_error(bus, "SaveAppearance", GLib.Variant("(ssday)", ("missing", "Name", 0.1, b"")), "does not exist")
            expect_error(bus, "SaveAppearance", GLib.Variant("(ssday)", ("../dusk", "Name", 0.1, b"")), "invalid")
            expect_error(bus, "SaveAppearance", GLib.Variant("(ssday)", ("dusk", "Name", 0.1, b"")), "does not exist")
            assert not (store / "dusk.desktop").exists()

            count = len(list(store.glob("*.desktop")))
            for index in range(count, 32):
                save("", f"Filler {index}", 0.4, sunset)
            expect_error(bus, "SaveAppearance", GLib.Variant("(ssday)", ("", "One too many", 0.4, sunset)), "Too many")

            delete("my-sunset-2")
            assert not (store / "my-sunset-2.desktop").exists()
            assert not (store / "my-sunset-2-wallpaper.png").exists()
            expect_error(bus, "ApplyAppearance", GLib.Variant("(s)", ("my-sunset-2",)), "")
            for identifier in ("missing", "../dusk", "dusk"):
                try:
                    delete(identifier)
                except GLib.Error:
                    continue
                raise AssertionError("delete unexpectedly succeeded for " + identifier)
            assert (appearances / "dusk.desktop").exists()
            def stored(identifier):
                call(bus, "ApplyAppearance", GLib.Variant("(s)", (identifier,)), "(s)")
                digest_now = call(bus, "GetCurrent", None, "(ss)").unpack()[1]
                data = (root / f"var/openwave/wallpaper/users/{uid}/{digest_now}.png"
                        if uid else root / f"var/openwave/wallpaper/{digest_now}.png").read_bytes()
                image = Image.open(io.BytesIO(data))
                assert image.size == (768, 1366), image.size
                return image.convert("RGB")

            def install(identifier, data, extension):
                (appearances / f"{identifier}.{extension}").write_bytes(data)
                (appearances / f"{identifier}.desktop").write_text(
                    "[Desktop Entry]\nType=X-OpenWave-Appearance\nName=X\n"
                    f"X-OpenWave-Appearance-Wallpaper={identifier}.{extension}\n")

            def near(pixel, expected, tolerance=6):
                return all(abs(a - b) <= tolerance for a, b in zip(pixel, expected))

            def bands(size, vertical):
                image = Image.new("RGB", size)
                for index, color in enumerate(((255, 0, 0), (0, 255, 0), (0, 0, 255))):
                    if vertical:
                        box = (size[0] * index // 3, 0, size[0] * (index + 1) // 3, size[1])
                    else:
                        box = (0, size[1] * index // 3, size[0], size[1] * (index + 1) // 3)
                    image.paste(color, box)
                return image

            landscape = io.BytesIO()
            bands((3000, 1000), True).save(landscape, "PNG")
            install("landscape", landscape.getvalue(), "png")
            wide = stored("landscape")
            assert near(wide.getpixel((10, 10)), (0, 255, 0)) and near(wide.getpixel((760, 1350)), (0, 255, 0))

            portrait = io.BytesIO()
            bands((1000, 4000), False).save(portrait, "JPEG", quality=95)
            install("portrait", portrait.getvalue(), "jpg")
            tall = stored("portrait")
            assert near(tall.getpixel((384, 683)), (0, 255, 0), 40)
            assert not near(tall.getpixel((384, 683)), (255, 0, 0), 40)

            tiny = io.BytesIO()
            Image.new("RGB", (40, 40), (90, 140, 200)).save(tiny, "PNG")
            install("tiny", tiny.getvalue(), "png")
            assert near(stored("tiny").getpixel((400, 700)), (90, 140, 200))

            translucent = io.BytesIO()
            Image.new("RGBA", (500, 900), (255, 255, 255, 128)).save(translucent, "PNG")
            install("translucent", translucent.getvalue(), "png")
            assert near(stored("translucent").getpixel((100, 100)), (128, 128, 128), 3)

            for mode, name in (("L", "gray"), ("P", "palette"), ("I;16", "sixteen")):
                buffer = io.BytesIO()
                Image.new(mode, (900, 700)).save(buffer, "PNG")
                install(name, buffer.getvalue(), "png")
                stored(name)
            progressive = io.BytesIO()
            Image.new("RGB", (1200, 800), (10, 200, 30)).save(progressive, "JPEG", progressive=True)
            install("progressive", progressive.getvalue(), "jpg")
            assert near(stored("progressive").getpixel((400, 600)), (10, 200, 30), 12)
            gray_jpeg = io.BytesIO()
            Image.new("L", (900, 900), 100).save(gray_jpeg, "JPEG")
            install("grayjpeg", gray_jpeg.getvalue(), "jpg")
            assert near(stored("grayjpeg").getpixel((300, 300)), (100, 100, 100), 8)
            cmyk = io.BytesIO()
            Image.new("CMYK", (300, 300), (0, 0, 0, 0)).save(cmyk, "JPEG")
            install("cmyk", cmyk.getvalue(), "jpg")
            expect_error(bus, "ApplyAppearance", GLib.Variant("(s)", ("cmyk",)), "")
            giant = io.BytesIO()
            Image.MAX_IMAGE_PIXELS = None
            Image.new("L", (9000, 8000), 7).save(giant, "PNG", compress_level=1)
            install("giant", giant.getvalue(), "png")
            expect_error(bus, "ApplyAppearance", GLib.Variant("(s)", ("giant",)), "")
            install("notanimage", b"GIF89a" + b"\0" * 100, "gif")
            expect_error(bus, "ApplyAppearance", GLib.Variant("(s)", ("notanimage",)), "")

            photo = io.BytesIO()
            bands((2000, 1500), True).save(photo, "JPEG", quality=92)
            made = save("", "From Photo", 0.3, photo.getvalue())
            assert near(stored(made).getpixel((384, 683)), (0, 255, 0), 40)
            odd = call(bus, "SetWallpaper", GLib.Variant("(ay)", (tiny.getvalue(),)), "(s)")[0]
            assert len(odd) == 64
            print("ApplyAppearance, SaveAppearance and DeleteAppearance behaved, any PNG or JPEG size was cover-cropped, and every invalid request was refused")
        finally:
            service.terminate()
            service.wait(timeout=5)


if __name__ == "__main__":
    main()
