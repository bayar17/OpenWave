#!/usr/bin/env python3
"""Run under dbus-run-session with a PORTAL_TESTING daemon launcher argument."""

import os
from pathlib import Path
import pwd
import subprocess
import sys
import tempfile
import time

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

ROOT = Path(__file__).resolve().parents[3]
BUS = "org.openwave.portal"
PATH = "/org/openwave/portal/FileChooser"
IFACE = "org.openwave.portal.FileChooser"


def connect():
    return Gio.DBusConnection.new_for_address_sync(
        os.environ["DBUS_SESSION_BUS_ADDRESS"],
        Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT
        | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
        None,
        None,
    )


def call(connection, method, parameters=None, reply_type=None):
    return connection.call_sync(
        BUS, PATH, IFACE, method, parameters,
        GLib.VariantType.new(reply_type) if reply_type else None,
        Gio.DBusCallFlags.NONE, 5000, None,
    )


def options(**values):
    encoded = {}
    for key, value in values.items():
        encoded[key] = GLib.Variant("as" if isinstance(value, list) else "b" if isinstance(value, bool) else "s", value)
    return GLib.Variant("(a{sv})", (encoded,))


def expect_error(action, expected):
    try:
        action()
    except GLib.Error as error:
        assert expected in error.message, error.message
        return
    raise AssertionError(f"Expected {expected} error")


def main():
    os.environ["DBUS_SYSTEM_BUS_ADDRESS"] = os.environ["DBUS_SESSION_BUS_ADDRESS"]
    with tempfile.TemporaryDirectory(prefix="openwave-portal-test-") as directory:
        storage = Path(directory) / "Data" / "Users" / pwd.getpwuid(os.getuid()).pw_name
        storage.mkdir(parents=True)
        image = storage / "sample.png"
        image.write_bytes(bytes.fromhex(
            "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c489"
            "0000000b49444154789c636000020000050001a5f645400000000049454e44ae426082"
        ))
        other = Path(directory) / "outside.png"
        other.write_bytes(image.read_bytes())
        binary = Path(sys.argv[1]).resolve()
        process = subprocess.Popen([
            str(binary), "--test-root", directory, os.path.realpath(sys.executable),
        ], env=os.environ.copy())
        try:
            backend = connect()
            requester = connect()
            for _ in range(50):
                try:
                    call(requester, "OpenFile", options(), "(o)")
                except GLib.Error as error:
                    if "BackendUnavailable" in error.message:
                        break
                    if "ServiceUnknown" not in error.message:
                        raise
                time.sleep(0.1)
            else:
                raise AssertionError("Portal did not start")

            call(backend, "RegisterBackend", reply_type="()")
            expect_error(
                lambda: call(requester, "OpenFile", options(initial_directory="/etc"), "(o)"),
                "InvalidRequest",
            )
            expect_error(
                lambda: call(requester, "OpenFile", GLib.Variant("(a{sv})", (
                    {"title": GLib.Variant("u", 9)},)), "(o)"),
                "InvalidRequest",
            )
            request = call(requester, "OpenFile", options(
                title="Choose wallpaper", muted_title="Choose",
                primary_title="wallpaper", mime_types=["image/png"]), "(o)")[0]
            next_request = call(backend, "NextRequest", reply_type="(osa{sv})")
            assert next_request[0] == request
            assert next_request[1] == "OpenFile"
            assert next_request[2]["muted_title"] == "Choose"
            assert next_request[2]["primary_title"] == "wallpaper"
            assert next_request[2]["application_id"] == Path(os.path.realpath(sys.executable)).name
            expect_error(
                lambda: call(requester, "CompleteRequest",
                    GLib.Variant("(ouas)", (request, 1, [str(image)])), "()"),
                "Unauthorized",
            )
            expect_error(
                lambda: call(backend, "CompleteRequest",
                    GLib.Variant("(ouas)", (request, 1, [str(other)])), "()"),
                "InvalidSelection",
            )
            call(backend, "CompleteRequest",
                GLib.Variant("(ouas)", (request, 1, [str(image)])), "()")
            assert call(requester, "GetStatus", GLib.Variant("(o)", (request,)), "(us)")[0] == 1
            result, fds = requester.call_with_unix_fd_list_sync(
                BUS, PATH, IFACE, "TakeResult", GLib.Variant("(o)", (request,)),
                GLib.VariantType.new("(uasah)"), Gio.DBusCallFlags.NONE,
                5000, None, None,
            )
            assert result[0] == 1 and result[1] == ["sample.png"]
            descriptor = fds.get(result[2][0])
            assert os.read(descriptor, 8) == b"\x89PNG\r\n\x1a\n"
            os.close(descriptor)
            expect_error(lambda: call(requester, "GetStatus",
                GLib.Variant("(o)", (request,)), "(us)"), "UnknownRequest")

            cancelled = call(requester, "OpenFile", options(), "(o)")[0]
            call(requester, "CancelRequest", GLib.Variant("(o)", (cancelled,)), "()")
            assert call(requester, "GetStatus",
                GLib.Variant("(o)", (cancelled,)), "(us)")[0] == 2
            call(requester, "TakeResult", GLib.Variant("(o)", (cancelled,)), "(uasah)")

            text = storage / "wrong-type.txt"
            text.write_text("not an image")
            rejected = call(requester, "OpenFile", options(mime_types=["image/png"]), "(o)")[0]
            expect_error(
                lambda: call(backend, "CompleteRequest",
                    GLib.Variant("(ouas)", (rejected, 1, [str(text)])), "()"),
                "InvalidSelection",
            )
            call(requester, "CancelRequest", GLib.Variant("(o)", (rejected,)), "()")
            call(requester, "TakeResult", GLib.Variant("(o)", (rejected,)), "(uasah)")

            # Settings requests image/*; malformed wildcards must still fail.
            for invalid in ("image/p*", "image/**", "*/png", "image/"):
                expect_error(lambda: call(requester, "OpenFile", options(mime_types=[invalid]), "(o)"), "InvalidRequest")
            expect_error(lambda: call(requester, "OpenFile", options(image_source="../outside"), "(o)"), "InvalidRequest")
            pictures = storage / "Pictures"
            pictures.mkdir()
            gallery_image = pictures / "photo.png"
            gallery_image.write_bytes(image.read_bytes())
            (pictures / "outside.png").symlink_to(other)
            (pictures / "outside-directory").symlink_to(other.parent)
            gallery = call(requester, "OpenFile", options(mime_types=["image/*"], image_sources=True, image_source="gallery"), "(o)")[0]
            next_gallery = call(backend, "NextRequest", reply_type="(osa{sv})")
            assert next_gallery[0] == gallery and next_gallery[2]["image_source"] == "gallery"
            expect_error(lambda: call(requester, "ListGallery", GLib.Variant("(o)", (gallery,)), "(a(ssss))"), "Unauthorized")
            entries = call(backend, "ListGallery", GLib.Variant("(o)", (gallery,)), "(a(ssss))")[0]
            assert len(entries) == 1 and entries[0][1:] == ("photo.png", "Pictures", "image/png"), entries
            token = entries[0][0]
            expect_error(lambda: call(backend, "OpenGalleryItem", GLib.Variant("(os)", (gallery, "forged-token")), "(h)"), "InvalidSelection")
            preview, fds = backend.call_with_unix_fd_list_sync(BUS, PATH, IFACE, "OpenGalleryItem", GLib.Variant("(os)", (gallery, token)), GLib.VariantType.new("(h)"), Gio.DBusCallFlags.NONE, 5000, None, None)
            descriptor = fds.get(preview[0]); assert os.read(descriptor, 8) == b"\x89PNG\r\n\x1a\n"; os.close(descriptor)
            cross = call(requester, "OpenFile", options(mime_types=["image/*"]), "(o)")[0]
            expect_error(lambda: call(backend, "OpenGalleryItem", GLib.Variant("(os)", (cross, token)), "(h)"), "InvalidSelection")
            call(requester, "CancelRequest", GLib.Variant("(o)", (cross,)), "()")
            call(requester, "TakeResult", GLib.Variant("(o)", (cross,)), "(uasah)")
            call(backend, "CompleteGalleryRequest", GLib.Variant("(ouas)", (gallery, 1, [token])), "()")
            selected, fds = requester.call_with_unix_fd_list_sync(BUS, PATH, IFACE, "TakeResult", GLib.Variant("(o)", (gallery,)), GLib.VariantType.new("(uasah)"), Gio.DBusCallFlags.NONE, 5000, None, None)
            assert selected[0] == 1 and selected[1] == ["photo.png"]
            descriptor = fds.get(selected[2][0])
            try:
                os.write(descriptor, b"forbidden")
                raise AssertionError("Gallery grant was writable")
            except OSError:
                pass
            os.close(descriptor)
            changed = call(requester, "OpenFile", options(mime_types=["image/*"]), "(o)")[0]
            old_token = call(backend, "ListGallery", GLib.Variant("(o)", (changed,)), "(a(ssss))")[0][0][0]
            replacement = pictures / "replacement.png"; replacement.write_bytes(image.read_bytes()); os.replace(replacement, gallery_image)
            expect_error(lambda: call(backend, "OpenGalleryItem", GLib.Variant("(os)", (changed, old_token)), "(h)"), "InvalidSelection")
            expect_error(lambda: call(backend, "CompleteGalleryRequest", GLib.Variant("(ouas)", (changed, 1, [old_token])), "()"), "InvalidSelection")
            call(requester, "CancelRequest", GLib.Variant("(o)", (changed,)), "()")
            call(requester, "TakeResult", GLib.Variant("(o)", (changed,)), "(uasah)")

            saved = call(requester, "SaveFile", options(suggested_filename="new.txt"), "(o)")[0]
            destination = storage / "new.txt"
            call(backend, "CompleteRequest",
                GLib.Variant("(ouas)", (saved, 1, [str(destination)])), "()")
            result, fds = requester.call_with_unix_fd_list_sync(
                BUS, PATH, IFACE, "TakeResult", GLib.Variant("(o)", (saved,)),
                GLib.VariantType.new("(uasah)"), Gio.DBusCallFlags.NONE,
                5000, None, None,
            )
            descriptor = fds.get(result[2][0])
            os.write(descriptor, b"saved through grant")
            os.close(descriptor)
            assert destination.read_bytes() == b"saved through grant"
            assert destination.stat().st_uid == os.getuid()
            assert destination.stat().st_mode & 0o777 == 0o600

            downloads = storage / "Downloads"
            assert not downloads.exists()
            saved = call(requester, "SaveFile", options(initial_directory=str(downloads), suggested_filename="download.bin"), "(o)")[0]
            assert downloads.stat().st_uid == os.getuid()
            assert downloads.stat().st_mode & 0o777 == 0o700
            call(backend, "CompleteRequest", GLib.Variant("(ouas)", (saved, 1, [str(downloads / "download.bin")])), "()")
            result, fds = requester.call_with_unix_fd_list_sync(BUS, PATH, IFACE, "TakeResult",
                GLib.Variant("(o)", (saved,)), GLib.VariantType.new("(uasah)"), Gio.DBusCallFlags.NONE, 5000, None, None)
            descriptor = fds.get(result[2][0]); os.write(descriptor, b"download"); os.close(descriptor)
            assert (downloads / "download.bin").read_bytes() == b"download"
            # An existing entry is never overwritten; a symlink cannot redirect
            # the default Downloads directory outside the verified user root.
            saved = call(requester, "SaveFile", options(initial_directory=str(downloads), suggested_filename="download.bin"), "(o)")[0]
            expect_error(lambda: call(backend, "CompleteRequest", GLib.Variant("(ouas)", (saved, 1, [str(downloads / "download.bin")])), "()"), "InvalidSelection")
            call(requester, "CancelRequest", GLib.Variant("(o)", (saved,)), "()")
            call(requester, "TakeResult", GLib.Variant("(o)", (saved,)), "(uasah)")
            downloads.rename(storage / "kept-downloads")
            downloads.symlink_to(Path(directory), target_is_directory=True)
            expect_error(lambda: call(requester, "SaveFile", options(initial_directory=str(downloads), suggested_filename="escape.bin"), "(o)"), "InvalidRequest")
            assert not (Path(directory) / "escape.bin").exists()
            downloads.unlink()
            (storage / "kept-downloads").rename(downloads)

            second = storage / "second.png"
            second.write_bytes(image.read_bytes())
            multiple = call(requester, "OpenFiles", options(mime_types=["image/png"]), "(o)")[0]
            call(backend, "CompleteRequest",
                GLib.Variant("(ouas)", (multiple, 1, [str(image), str(second)])), "()")
            result, fds = requester.call_with_unix_fd_list_sync(
                BUS, PATH, IFACE, "TakeResult", GLib.Variant("(o)", (multiple,)),
                GLib.VariantType.new("(uasah)"), Gio.DBusCallFlags.NONE,
                5000, None, None,
            )
            assert result[1] == ["sample.png", "second.png"]
            for handle in result[2]:
                os.close(fds.get(handle))

            directory_request = call(requester, "SelectDirectory", options(), "(o)")[0]
            call(backend, "CompleteRequest",
                GLib.Variant("(ouas)", (directory_request, 1, [str(storage)])), "()")
            result, fds = requester.call_with_unix_fd_list_sync(
                BUS, PATH, IFACE, "TakeResult",
                GLib.Variant("(o)", (directory_request,)),
                GLib.VariantType.new("(uasah)"), Gio.DBusCallFlags.NONE,
                5000, None, None,
            )
            descriptor = fds.get(result[2][0])
            assert os.path.isdir(f"/proc/self/fd/{descriptor}")
            os.close(descriptor)

            transient = connect()
            orphaned = call(transient, "OpenFile", options(), "(o)")[0]
            transient.close_sync(None)
            for _ in range(30):
                if call(backend, "NextRequest", reply_type="(osa{sv})")[0] != orphaned:
                    break
                time.sleep(0.1)
            else:
                raise AssertionError("Request survived application termination")

            failed = call(requester, "OpenFile", options(), "(o)")[0]
            backend.close_sync(None)
            for _ in range(30):
                state = call(requester, "GetStatus", GLib.Variant("(o)", (failed,)), "(us)")
                if state[0] == 3:
                    assert "backend closed" in state[1]
                    break
                time.sleep(0.1)
            else:
                raise AssertionError("Backend disappearance did not fail the request")
            requester.close_sync(None)
            print("portal contract: open, multiple, save, directory, cancellation, authorization, image wildcards, gallery tokens, previews, readonly grants, replacement rejection and failure passed")
        finally:
            process.terminate()
            process.wait(timeout=5)


if __name__ == "__main__":
    main()
