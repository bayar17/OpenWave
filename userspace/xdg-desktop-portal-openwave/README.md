# OpenWave desktop settings portal

`xdg-desktop-portal-openwave` implements the read-only Settings portal. The default
mode owns both portal bus names on OpenWave's session bus and exports
`org.freedesktop.portal.Settings` version 2 at `/org/freedesktop/portal/desktop`.
This is a Settings-only public implementation, not the complete upstream portal
frontend. File selection, prompts, and permissions keep their existing native
OpenWave interfaces; no additional XDG capabilities are advertised.

`--backend` owns only `org.freedesktop.impl.portal.desktop.openwave` and exports
`org.freedesktop.impl.portal.Settings` version 2. The installed `openwave.portal`,
`openwave-portals.conf`, and session activation file allow the full upstream
xdg-desktop-portal frontend to discover that backend later. When adopting that
frontend, change the init entry to run only `--backend`. Two processes must not
own the public Desktop name.

The API contracts live in `userspace/dbus/interfaces/` and are loaded through
`OpenWave::Xml`, rather than embedded XML strings. `/init` supervises the public
service and backend in one process, sharing one preference snapshot and monitor.
OpenWave's current D-Bus was built without traditional activation, so runtime
startup does not depend on activation. The backend activation file supports
future D-Bus builds with that feature. There is no public activation file,
avoiding a startup race with init. Reserved names are owned
only by root in the OpenWave session policy. The service has its own SELinux
domain and is installed under `/System/Services`.

| Namespace | Key | Value |
| --- | --- | --- |
| `org.freedesktop.appearance` | `color-scheme` | uint32 `0`, no preference; no global light/dark selection exists yet |
| `org.freedesktop.appearance` | `contrast` | uint32 `0`, normal contrast |
| `org.freedesktop.appearance` | `reduced-motion` | uint32 `1` when `accessibility/reduceMotion` is enabled, otherwise `0` |
| `org.gnome.desktop.interface` | `font-name` | `Source Sans Pro`, the current system family, without imposing a toolkit font size |
| `org.gnome.desktop.interface` | `color-scheme` | `default` |
| `org.gnome.desktop.interface` | `enable-animations` | inverse of reduced motion |
| `org.gnome.desktop.interface` | `text-scaling-factor` | `1.2` for `accessibility/largeText`, otherwise `1.0` |

Only this allowlist is exported, never the whole preferences file. The preference
path comes from OpenWave's existing resolver. `--preferences FILE` and
`--interfaces DIRECTORY` are administrator/test overrides, not D-Bus methods.
Changes emit `SettingChanged` for changed values only. Directory monitoring
supports Qt's atomic file replacement, initially absent directories, and deletion.
Malformed settings retain the last valid snapshot.

`ReadAll` supports empty lists, empty patterns, exact namespaces, and trailing
section globs such as `org.freedesktop.*`. Unknown keys return
`org.freedesktop.portal.Error.NotFound`. Public `ReadOne` and backend `Read` wrap
the value once; deprecated public `Read` preserves its historical double variant.
There is no write API.

The design and protocol checks were researched using XERJ against
`flatpak/xdg-desktop-portal` commit
`8eba842f30070e2d90e16832462ad8171bd5beed`, particularly
`desktop-portal/settings.c`, `tests/test_settings.py`, and
`doc/writing-a-new-backend.rst`. Implementation code was written for OpenWave.

The regular `./build-openwave userspace` command builds and stages the service,
contracts, activation metadata, and SELinux policy. To test both APIs on an
isolated host session bus with a native executable:

```bash
dbus-run-session -- /usr/bin/python3 \
  userspace/xdg-desktop-portal-openwave/tests/settings_contract.py \
  /path/to/xdg-desktop-portal-openwave
```

For the target build on an x86_64 Linux development host, pass the SDK loader,
`--library-path` with the target sysroot libraries, then the target executable.
Tests cover filtering, variant types, read-only behavior, private-key exclusion,
live updates, atomic replacement, malformed preferences, restart persistence, and
duplicate name ownership. No display server or Qt Quick window is required.
