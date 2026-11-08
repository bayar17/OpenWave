#!/usr/bin/env python3
"""Run the production child environment builder for trusted and confined apps."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SYSROOT = ROOT / 'toolchain/sysroot/x86_64-unknown-openwave'

class EnvironmentTests(unittest.TestCase):
    def test_configured_session_socket_and_private_app_environment(self):
        with tempfile.TemporaryDirectory(prefix='openwave-environment-') as temporary:
            temporary = Path(temporary)
            source, output = temporary / 'environment.c', temporary / 'environment'
            source.write_text(r'''
#include "process.c"
gboolean appd_trusted_app(App *app) { return !strcmp(app->id, "com.openwave.settings"); }
int main(void)
{
    Manager manager = {.runtime = "/run/user/1000", .home = "/Data/Users/ocean", .user = "ocean", .uid = 1000};
    App app = {.id = "com.openwave.settings", .bundle = "/System/Applications/com.openwave.settings"};
    Instance instance = {.runtime = "/run/user/1000", .data = "/Data/Users/ocean/.DataLocation/Applications/com.openwave.settings"};
    g_setenv("LD_PRELOAD", "/untrusted.so", TRUE);
    char **env = environment(&manager, &app, &instance);
    g_assert_cmpstr(g_environ_getenv(env, "WAYLAND_DISPLAY"), ==, "/run/user/1000/wayland-0");
    g_assert_cmpstr(g_environ_getenv(env, "DBUS_SESSION_BUS_ADDRESS"), ==, "unix:path=/run/user/1000/bus");
    g_assert_null(g_environ_getenv(env, "LD_PRELOAD"));
    g_strfreev(env);
    manager.runtime = "/run/qa-session";
    env = environment(&manager, &app, &instance);
    g_assert_cmpstr(g_environ_getenv(env, "WAYLAND_DISPLAY"), ==, "/run/qa-session/wayland-0");
    g_strfreev(env);
    app.id = "org.openwave.qa.fixture";
    instance.runtime = "/run/openwave/apps/fixture";
    instance.data = "/Data/Users/ocean/.DataLocation/Applications/org.openwave.qa.fixture";
    env = environment(&manager, &app, &instance);
    g_assert_cmpstr(g_environ_getenv(env, "WAYLAND_DISPLAY"), ==, "/run/openwave/wayland-0");
    g_assert_cmpstr(g_environ_getenv(env, "DBUS_SESSION_BUS_ADDRESS"), ==, "unix:path=/run/openwave/session-bus");
    g_assert_cmpstr(g_environ_getenv(env, "HOME"), ==, instance.data);
    g_assert_cmpstr(g_environ_getenv(env, "XDG_RUNTIME_DIR"), ==, instance.runtime);
    g_assert_null(g_environ_getenv(env, "LD_PRELOAD"));
    g_strfreev(env);
    return 0;
}
''')
            subprocess.run([str(ROOT / 'toolchain/bin/clang'), '--target=x86_64-unknown-openwave',
                '--sysroot=' + str(SYSROOT), '-O2', '-ffunction-sections', '-fdata-sections',
                '-I' + str(ROOT / 'userspace/appd'), '-I' + str(ROOT / 'userspace/security/selinux'),
                '-I' + str(ROOT / 'toolchain/include'),
                '-I' + str(SYSROOT / 'usr/include/glib-2.0'),
                '-I' + str(SYSROOT / 'usr/lib64/glib-2.0/include'), str(source),
                '-Wl,--gc-sections', '-lgio-2.0', '-lgobject-2.0', '-lglib-2.0', '-o', str(output)], check=True)
            subprocess.run([str(SYSROOT / 'lib64/ld-linux-x86-64.so.2'), '--library-path',
                ':'.join(str(SYSROOT / p) for p in ('usr/lib64', 'usr/lib', 'lib64')), str(output)], check=True)

if __name__ == '__main__':
    unittest.main()
