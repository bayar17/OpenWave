# Upstream design references

Study the relevant implementation before adapting its approach to OpenWave.
Record the upstream repository and commit when a design informs a change.
Check the source license and write an OpenWave implementation; do not copy
upstream code.

| Group | Repositories | Relevant problems |
| --- | --- | --- |
| `shell` | [Plasma Mobile](https://github.com/KDE/plasma-mobile) | Qt Quick phone shell, app overview, gestures, panel transitions |
| `wayland` | [Sailfish Lipstick](https://github.com/sailfishos/lipstick), [Qt Wayland](https://github.com/qt/qtwayland) | Mobile compositor, focus, client surfaces, minimize and lifecycle protocols |
| `apps` | [Angelfish](https://github.com/KDE/angelfish), [MauiKit File Browsing](https://github.com/KDE/mauikit-filebrowsing) | Mobile browser tabs/history/downloads, file models and operations |
| `security` | [xdg-desktop-portal](https://github.com/flatpak/xdg-desktop-portal), [Bubblewrap](https://github.com/containers/bubblewrap) | D-Bus request identity, permissions, file chooser brokering, namespace isolation |

Angelfish uses Qt WebEngine; Ocean uses WPE/WebKit. Plasma Mobile uses KDE services, and Lipstick has its own Sailfish APIs. These references do not replace OpenWave's services, custom init, theme, or IPC contracts. Qt Wayland is a pinned upstream snapshot, not a promise of matching the exported SDK's Qt version.
