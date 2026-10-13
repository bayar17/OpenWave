# OpenWave wallpaper service

`wallpaperagentd` owns the current wallpaper on the system bus as
`org.openwave.Wallpaper`, object `/org/openwave/Wallpaper`, interface
`org.openwave.Wallpaper1`.

| Member | Signature | Purpose |
| --- | --- | --- |
| `GetCurrent` | `() → (ss)` | Return the verified PNG path and its SHA-256, or two empty strings when no valid wallpaper is selected. |
| `SetWallpaper` | `(ay) → (s)` | Accept a complete 768×1366 PNG and return its SHA-256 in the authenticated caller's storage. |
| `Changed` | `(ss)` | Announce the new path and SHA-256 after a successful update. |

For the current root session, the service writes
`/var/openwave/wallpaper/<sha256>.png` and stores the selected hash in
`/var/openwave/wallpaper/current`. Other users have separate storage under
`/var/openwave/wallpaper/users/<uid>/`. The service identifies the caller from
D-Bus credentials and uses that user's storage for both methods. `GetCurrent`
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
