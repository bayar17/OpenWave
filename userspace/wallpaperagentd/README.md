# OpenWave wallpaper service

`wallpaperagentd` owns the current wallpaper on the system bus as
`org.openwave.Wallpaper`, object `/org/openwave/Wallpaper`, interface
`org.openwave.Wallpaper1`.

| Member | Signature | Purpose |
| --- | --- | --- |
| `GetCurrent` | `() → (ss)` | Return the verified PNG path and its SHA-256, or two empty strings when no valid wallpaper is selected. |
| `GetImage` | `() → (ay)` | Return verified PNG bytes for the authenticated session user. |
| `SetWallpaper` | `(ay) → (s)` | Accept a complete 768×1366 PNG and return its SHA-256 in the authenticated caller's storage. |
| `Changed` | `(ss)` | Announce the new path and SHA-256 after a successful update. |

For the current root session, the service writes
`/var/openwave/wallpaper/<sha256>.png` and stores the selected hash in
`/var/openwave/wallpaper/current`. Other users have separate storage under
`/var/openwave/wallpaper/users/<uid>/`. Per-user directories belong to the broker
and have mode 0700. The service resolves non-root app identities through appd
and keys storage by their session user, rather than an isolated app's UID.
Writes require verified trusted `com.openwave.settings`, or root. The supervised
Ocean process can read the current session's wallpaper. Clients receive image
bytes rather than direct access to the service's private files. `GetCurrent`
checks the marker, file type, image dimensions, PNG decoding, and the SHA-256 of the stored file. A
failed check gives an empty path; clients render a black background in that case.

Qt Quick apps use `userspace/wallpaper-client/WallpaperClient.{h,cpp}`. Register
one `WallpaperClient` as the `Wallpaper` context property, then bind an `Image`'s
`source` to `Wallpaper.source` over a black `Rectangle`. The client refreshes
when the wallpaper changes. Settings uses `imageSize()` and
`setCroppedWallpaper()` for its movable 768×1366 crop. It receives source
images through `OpenWavePortals`.

Run the user-storage and integrity test with
`dbus-run-session -- python3 userspace/wallpaperagentd/tests/wallpaper_users.py`.

Run the writer-identity regression with
`python3 userspace/wallpaperagentd/tests/test_authorization.py`. Guest coverage
of the real Settings file/photo selection, crop and save flow is recorded in
[the recovery report](../../docs/PORTAL-WALLPAPER-OPENRC-RECOVERY.md).
