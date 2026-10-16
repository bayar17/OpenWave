#!/usr/bin/env python3
"""Private-bus tests against promptd compiled with PROMPTD_TESTING only."""
import os
import pathlib
import subprocess
import sys
import tempfile
import time
import unittest
from gi.repository import Gio, GLib

BUS = "org.openwave.Prompt"
PATH = "/org/openwave/Prompt"
IFACE = "org.openwave.Prompt1"
DAEMON = pathlib.Path(sys.argv.pop(1)).resolve()


class ServiceTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.pidfile = pathlib.Path(self.directory.name) / "renderer.pid"
        self.pidfile.write_text(str(os.getpid()))
        self.dbus = subprocess.Popen(["dbus-daemon", "--session", "--nofork", "--print-address=1"],
                                     stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        self.address = self.dbus.stdout.readline().strip()
        self.connections = []
        self.start_service()
        self.renderer = self.connection()
        self.call(self.renderer, "RegisterRenderer")
        self.client = self.connection()

    def start_service(self):
        self.daemon = subprocess.Popen([str(DAEMON), os.readlink("/proc/self/exe"), str(self.pidfile)],
                                      env={**os.environ, "DBUS_SESSION_BUS_ADDRESS": self.address},
                                      stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        probe = self.connection()
        for _ in range(100):
            try:
                probe.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus",
                    "org.freedesktop.DBus", "GetNameOwner", GLib.Variant("(s)", (BUS,)),
                    None, Gio.DBusCallFlags.NONE, 1000, None)
                return
            except GLib.Error:
                time.sleep(.02)
        self.fail("promptd failed to acquire its name")

    def connection(self):
        connection = Gio.DBusConnection.new_for_address_sync(self.address,
            Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
            None, None)
        connection.set_exit_on_close(False)
        self.connections.append(connection)
        return connection

    def call(self, connection, method, signature="()", values=()):
        return connection.call_sync(BUS, PATH, IFACE, method, GLib.Variant(signature, values),
                                    None, Gio.DBusCallFlags.NONE, 2000, None).unpack()

    def request(self, kind="confirmation", **options):
        fields = {"title": GLib.Variant("s", "Replace file?"), "message": GLib.Variant("s", "Keep original on cancel.")}
        fields.update({k: GLib.Variant("u" if isinstance(v, int) else "s", v) for k, v in options.items()})
        return self.call(self.client, "Request", "(sa{sv})", (kind, fields))[0]

    def error(self, suffix, connection, method, signature="()", values=()):
        with self.assertRaises(GLib.Error) as caught:
            self.call(connection, method, signature, values)
        self.assertIn("org.openwave.Prompt.Error." + suffix, str(caught.exception))

    def test_secret_input_options_and_private_result(self):
        options = {"title": GLib.Variant("s", "Network"), "secret": GLib.Variant("b", True),
                   "allowPasswordReveal": GLib.Variant("b", True), "minLength": GLib.Variant("u", 8)}
        prompt = self.call(self.client, "Request", "(sa{sv})", ("text_input", options))[0]
        current, kind, rendered = self.call(self.renderer, "Next")
        self.assertEqual(current, prompt)
        self.assertEqual(kind, "text_input")
        self.assertTrue(rendered["secret"])
        self.assertTrue(rendered["allowPasswordReveal"])
        self.call(self.renderer, "Respond", "(sss)", (prompt, "accepted", "test-password"))
        self.assertEqual(self.call(self.client, "GetResult", "(s)", (prompt,)), (True, "accepted", "test-password"))
        other = self.connection()
        self.error("Unauthorized", other, "GetResult", "(s)", (prompt,))
        for kind, invalid in [("confirmation", options),
                              ("text_input", {**options, "secret": GLib.Variant("s", "true")}),
                              ("text_input", {**options, "allowPasswordReveal": GLib.Variant("u", 1)}),
                              ("text_input", {**options, "secret": GLib.Variant("b", False)})]:
            self.error("InvalidRequest", self.client, "Request", "(sa{sv})", (kind, invalid))

    def test_fifo_unique_retained_results(self):
        first, second = self.request(), self.request()
        self.assertNotEqual(first, second)
        for _ in range(3):
            self.assertEqual(self.call(self.renderer, "Next")[0], first)
        self.error("InvalidResponse", self.renderer, "Respond", "(sss)", (second, "accepted", ""))
        self.call(self.renderer, "Respond", "(sss)", (first, "accepted", ""))
        self.assertEqual(self.call(self.renderer, "Next")[0], second)
        for _ in range(3):
            self.assertEqual(self.call(self.client, "GetResult", "(s)", (first,)), (True, "accepted", ""))
        self.call(self.client, "Acknowledge", "(s)", (first,))
        self.error("UnknownRequest", self.client, "GetResult", "(s)", (first,))

    def test_connection_ownership(self):
        request = self.request()
        outsider = self.connection()
        for method in ("Cancel", "GetResult", "Acknowledge"):
            self.error("Unauthorized", outsider, method, "(s)", (request,))
        self.error("Unauthorized", outsider, "Next")
        self.error("Unauthorized", outsider, "Respond", "(sss)", (request, "accepted", ""))
        self.error("Busy", outsider, "RegisterRenderer")
        self.assertEqual(self.call(self.client, "GetResult", "(s)", (request,)), (False, "", ""))

    def test_two_requesting_connections(self):
        first_client = self.client
        first = self.request()
        self.client = second_client = self.connection()
        second = self.request()
        self.assertEqual(self.call(self.renderer, "Next")[0], first)
        self.call(self.renderer, "Respond", "(sss)", (first, "accepted", ""))
        self.assertEqual(self.call(self.renderer, "Next")[0], second)
        self.error("Unauthorized", second_client, "GetResult", "(s)", (first,))
        self.assertEqual(self.call(first_client, "GetResult", "(s)", (first,)), (True, "accepted", ""))
        self.call(self.renderer, "Respond", "(sss)", (second, "cancelled", ""))
        self.assertEqual(self.call(second_client, "GetResult", "(s)", (second,)), (True, "cancelled", ""))

    def test_owner_pid_is_derived_from_connection(self):
        request = self.request()
        identifier, kind, fields = self.call(self.renderer, "Next")
        self.assertEqual(identifier, request)
        self.assertEqual(fields["ownerPid"], os.getpid())

    def test_renderer_credentials(self):
        self.pidfile.write_text("1")
        self.error("Unauthorized", self.connection(), "RegisterRenderer")

    def test_invalid_requests(self):
        for kind, options in (("password", {}), ("confirmation", {"applicationId": "settings"}),
                              ("confirmation", {"ownerPid": 1}),
                              ("confirmation", {"title": "x" * 161}), ("confirmation", {"timeoutMs": 0}),
                              ("text_input", {"maxLength": 513}), ("text_input", {"validationPattern": "["}),
                              ("text_input", {"minLength": 10, "maxLength": 3}),
                              ("message", {"initialText": "secret"})):
            with self.assertRaises(GLib.Error) as caught:
                self.request(kind, **options)
            self.assertIn("InvalidRequest", str(caught.exception))
        self.assertEqual(self.call(self.renderer, "Next")[0], "")

    def test_text_validation(self):
        request = self.request("text_input", validationPattern="[A-Z]{2,4}", minLength=2, maxLength=4)
        fields = self.call(self.renderer, "Next")[2]
        self.assertEqual((fields["minLength"], fields["maxLength"]), (2, 4))
        for value in ("a", "ABCDE", "ab", "AB\n"):
            self.error("InvalidResponse", self.renderer, "Respond", "(sss)", (request, "accepted", value))
        self.call(self.renderer, "Respond", "(sss)", (request, "accepted", "ABC"))
        self.assertEqual(self.call(self.client, "GetResult", "(s)", (request,)), (True, "accepted", "ABC"))

    @staticmethod
    def variant(value):
        if isinstance(value, dict):
            return GLib.Variant("a{sv}", {k: ServiceTest.variant(v) for k, v in value.items()})
        return GLib.Variant("b" if isinstance(value, bool) else "u" if isinstance(value, int) else "s", value)

    def network_form(self, **changes):
        options = {"title": "Add Network", "fieldOrder": "ssid,security,password", "fields": {
            "ssid": {"minLength": 1, "maxLength": 32, "maxBytes": 32},
            "security": {"initialText": "wpa", "choices": {"wpa": "WPA Personal", "open": "Open"}},
            "password": {"secret": True, "minLength": 8, "maxLength": 63,
                         "activeWhenField": "security", "activeWhenValue": "wpa"}}}
        options.update(changes)
        return self.call(self.client, "Request", "(sa{sv})", ("form", {k: self.variant(v) for k, v in options.items()}))[0]

    def test_form_validation_and_password_privacy(self):
        request = self.network_form()
        self.assertEqual(self.call(self.renderer, "Next")[1], "form")
        for fields in ({"ssid": "Network", "security": "wpa", "password": "short"},
                       {"ssid": "あ" * 11, "security": "wpa", "password": "password"},
                       {"ssid": "Network", "security": "unknown", "password": "password"},
                       {"ssid": "Network", "security": "open", "password": "password"},
                       {"ssid": "Network", "security": "wpa"},
                       {"ssid": "Network", "security": "wpa", "password": "password", "extra": "x"}):
            text = GLib.Variant("a{ss}", fields).print_(False)
            self.error("InvalidResponse", self.renderer, "Respond", "(sss)", (request, "accepted", text))
        for text in ("not a dictionary", "{'ssid': 'x', 'ssid': 'y', 'security': 'open'}"):
            self.error("InvalidResponse", self.renderer, "Respond", "(sss)", (request, "accepted", text))
        valid = GLib.Variant("a{ss}", {"ssid": "Network", "security": "wpa", "password": "password"}).print_(False)
        self.error("Unauthorized", self.connection(), "Respond", "(sss)", (request, "accepted", valid))
        self.call(self.renderer, "Respond", "(sss)", (request, "accepted", valid))
        self.assertEqual(self.call(self.client, "GetResult", "(s)", (request,)), (True, "accepted", valid))
        self.error("Unauthorized", self.connection(), "GetResult", "(s)", (request,))
        self.call(self.client, "Acknowledge", "(s)", (request,))
        opened = self.network_form()
        self.call(self.renderer, "Next")
        text = GLib.Variant("a{ss}", {"ssid": "Open network", "security": "open", "password": ""}).print_(False)
        self.call(self.renderer, "Respond", "(sss)", (opened, "accepted", text))
        self.assertEqual(self.call(self.client, "GetResult", "(s)", (opened,)), (True, "accepted", text))
        cancelled = self.network_form()
        self.call(self.renderer, "Next")
        self.error("InvalidResponse", self.renderer, "Respond", "(sss)", (cancelled, "cancelled", valid))
        self.call(self.client, "Cancel", "(s)", (cancelled,))
        self.assertEqual(self.call(self.client, "GetResult", "(s)", (cancelled,)), (True, "cancelled", ""))

    def test_six_field_ip_dns_form(self):
        fields = {
            "ipMode": {"label": "IP configuration", "initialText": "manual",
                       "choices": {"automatic": "Automatic (DHCP)", "manual": "Manual"}},
            "address": {"label": "IPv4 address", "initialText": "192.168.1.50", "minLength": 1,
                        "maxLength": 15, "activeWhenField": "ipMode", "activeWhenValue": "manual"},
            "prefix": {"label": "Subnet prefix (0–32)", "initialText": "24", "minLength": 1,
                       "maxLength": 2, "activeWhenField": "ipMode", "activeWhenValue": "manual"},
            "gateway": {"label": "Gateway (optional)", "initialText": "", "maxLength": 15,
                        "activeWhenField": "ipMode", "activeWhenValue": "manual"},
            "dnsMode": {"label": "DNS configuration", "initialText": "manual",
                        "choices": {"automatic": "Automatic", "manual": "Manual"}},
            "dnsServers": {"label": "DNS servers", "initialText": "1.1.1.1, 2001:4860:4860::8888",
                           "minLength": 1, "maxLength": 512,
                           "activeWhenField": "dnsMode", "activeWhenValue": "manual"}}
        for manual in (True, False):
            request = self.network_form(title="IP and DNS settings", fieldOrder=",".join(fields), fields=fields)
            self.assertEqual(self.call(self.renderer, "Next")[1], "form")
            values = {key: value["initialText"] for key, value in fields.items()}
            if not manual:
                values.update(ipMode="automatic", address="", prefix="", gateway="",
                              dnsMode="automatic", dnsServers="")
            text = GLib.Variant("a{ss}", values).print_(False)
            self.call(self.renderer, "Respond", "(sss)", (request, "accepted", text))
            self.assertEqual(self.call(self.client, "GetResult", "(s)", (request,)), (True, "accepted", text))
            self.call(self.client, "Acknowledge", "(s)", (request,))

    def test_invalid_form_schemas(self):
        for changes in ({"fieldOrder": "ssid,ssid"}, {"fieldOrder": "unknown"}, {"fields": {}},
                        {"fields": {"ssid": {"secret": "true"}}},
                        {"fieldOrder": "ssid", "fields": {"ssid": {"choices": {"open": "Open"}, "initialText": "unknown"}}},
                        {"fieldOrder": "ssid", "fields": {"ssid": {"activeWhenField": "missing", "activeWhenValue": "open"}}},
                        {"fieldOrder": "ssid", "fields": {"ssid": {"maxBytes": 0}}},
                        {"fieldOrder": "ssid", "fields": {"ssid": {"unknownOption": "x"}}}):
            with self.assertRaises(GLib.Error) as caught:
                self.network_form(**changes)
            self.assertIn("InvalidRequest", str(caught.exception))
        self.assertEqual(self.call(self.renderer, "Next")[0], "")

    def test_selection_choice_validation_and_large_list(self):
        choices = {"Region/City_" + str(i): self.variant("Region/City " + str(i)) for i in range(600)}
        options = {"title": self.variant("Time zone"), "choices": GLib.Variant("a{sv}", choices),
                   "initialText": self.variant("Region/City_599"), "placeholder": self.variant("Search city or region")}
        request = self.call(self.client, "Request", "(sa{sv})", ("selection", options))[0]
        fields = self.call(self.renderer, "Next")[2]
        self.assertEqual(len(fields["choices"]), 600)
        for value in ("", "Unknown/Zone", "Region/City_1\n"):
            self.error("InvalidResponse", self.renderer, "Respond", "(sss)", (request, "accepted", value))
        self.error("InvalidResponse", self.renderer, "Respond", "(sss)", (request, "cancelled", "Region/City_1"))
        self.call(self.renderer, "Respond", "(sss)", (request, "accepted", "Region/City_42"))
        self.assertEqual(self.call(self.client, "GetResult", "(s)", (request,)), (True, "accepted", "Region/City_42"))

    def test_settings_permission_choice_and_confirmation(self):
        choices = {"granted": "Allow", "denied": "Deny",
                   "not-determined": "Ask next time", "restricted": "Restricted"}
        options = {"title": self.variant("Save files"),
                   "message": self.variant("Choose how this application may use the file chooser."),
                   "choices": self.variant(choices), "initialText": self.variant("granted"),
                   "acceptLabel": self.variant("Apply"), "cancelLabel": self.variant("Cancel")}
        prompt = self.call(self.client, "Request", "(sa{sv})", ("selection", options))[0]
        current, kind, rendered = self.call(self.renderer, "Next")
        self.assertEqual((current, kind), (prompt, "selection"))
        self.assertEqual(rendered["choices"], choices)
        self.assertEqual(rendered["acceptLabel"], "Apply")
        self.assertEqual(rendered["cancelLabel"], "Cancel")
        self.call(self.renderer, "Respond", "(sss)", (prompt, "accepted", "denied"))
        self.assertEqual(self.call(self.client, "GetResult", "(s)", (prompt,)), (True, "accepted", "denied"))
        confirmation = self.request(acceptLabel="Change", cancelLabel="Cancel")
        current, kind, rendered = self.call(self.renderer, "Next")
        self.assertEqual((current, kind), (confirmation, "confirmation"))
        self.assertEqual(rendered["acceptLabel"], "Change")
        self.call(self.renderer, "Respond", "(sss)", (confirmation, "cancelled", ""))
        self.assertEqual(self.call(self.client, "GetResult", "(s)", (confirmation,)), (True, "cancelled", ""))

    def test_invalid_selection_requests(self):
        for choices, initial in (({}, ""), ({"UTC": "UTC"}, "Unknown/Zone"),
                                 ({"": "Empty key"}, ""), ({"bad\nkey": "UTC"}, ""),
                                 ({"UTC": 1}, ""), ({"City_" + str(i): "City" for i in range(1025)}, "")):
            options = {"title": self.variant("Time zone"), "choices": self.variant(choices), "initialText": self.variant(initial)}
            self.error("InvalidRequest", self.client, "Request", "(sa{sv})", ("selection", options))
        self.assertEqual(self.call(self.renderer, "Next")[0], "")

    def test_explicit_cancel_and_timeout(self):
        request = self.request()
        self.call(self.renderer, "Next")
        self.call(self.client, "Cancel", "(s)", (request,))
        self.error("InvalidResponse", self.renderer, "Respond", "(sss)", (request, "accepted", ""))
        self.assertEqual(self.call(self.client, "GetResult", "(s)", (request,)), (True, "cancelled", ""))
        timed = self.request(timeoutMs=1000)
        time.sleep(1.4)
        self.assertEqual(self.call(self.client, "GetResult", "(s)", (timed,)), (True, "timed_out", ""))

    def test_app_disconnect(self):
        self.request()
        self.call(self.renderer, "Next")
        self.client.close_sync(None)
        time.sleep(.1)
        self.assertEqual(self.call(self.renderer, "Next")[0], "")

    def test_renderer_restart(self):
        first, second = self.request(), self.request()
        self.call(self.renderer, "Next")
        self.renderer.close_sync(None)
        time.sleep(.1)
        for request in (first, second):
            self.assertEqual(self.call(self.client, "GetResult", "(s)", (request,)), (True, "renderer_lost", ""))
        self.renderer = self.connection()
        self.call(self.renderer, "RegisterRenderer")
        self.assertEqual(self.call(self.renderer, "Next")[0], "")
        request = self.request()
        self.assertEqual(self.call(self.renderer, "Next")[0], request)

    def test_service_restart(self):
        request = self.request()
        self.call(self.renderer, "Next")
        self.daemon.terminate()
        self.daemon.communicate(timeout=3)
        self.start_service()
        self.call(self.renderer, "RegisterRenderer")
        self.assertEqual(self.call(self.renderer, "Next")[0], "")
        self.error("UnknownRequest", self.client, "GetResult", "(s)", (request,))
        self.assertNotEqual(self.request(), request)

    def test_outstanding_limit(self):
        for _ in range(4):
            self.request()
        with self.assertRaises(GLib.Error) as caught:
            self.request()
        self.assertIn("Busy", str(caught.exception))

    def test_rate_limit(self):
        for _ in range(12):
            request = self.request()
            self.call(self.client, "Cancel", "(s)", (request,))
            self.call(self.client, "Acknowledge", "(s)", (request,))
        with self.assertRaises(GLib.Error) as caught:
            self.request()
        self.assertIn("Busy", str(caught.exception))

    def tearDown(self):
        for connection in self.connections:
            if not connection.is_closed():
                connection.close_sync(None)
        self.daemon.terminate()
        _, errors = self.daemon.communicate(timeout=3)
        self.assertNotIn("CRITICAL", errors)
        self.dbus.terminate()
        self.dbus.communicate(timeout=3)
        self.directory.cleanup()


unittest.main()
