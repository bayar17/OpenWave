# OpenWave wallpaper service

`wallpaperagentd` owns the current wallpaper on the system bus as
`org.openwave.Wallpaper`, object `/org/openwave/Wallpaper`, interface
`org.openwave.Wallpaper1`.

| Member | Signature | Purpose |
| --- | --- | --- |
| `GetCurrent` | `() → (ss)` | Return the verified PNG path and its SHA-256, or two empty strings when no valid wallpaper is selected. |
| `GetImage` | `() → (ay)` | Return verified PNG bytes for the authenticated session user. |
| `SetWallpaper` | `(ay) → (s)` | Accept a PNG or JPEG of any resolution, cover-crop it to 768×1366 when needed, and return the stored image's SHA-256 in the authenticated caller's storage. |
| `ApplyAppearance` | `(s) → (s)` | Apply the wallpaper of an installed appearance by id and return its SHA-256. The daemon itself reads `<id>.desktop` from `/System/QuickMenu/Appearances` (then the user's store `/var/openwave/appearances/users/<uid>/`), takes its `X-OpenWave-Appearance-Wallpaper` file name, and accepts only a plain file name next to it. The image may be a PNG or JPEG of any resolution (at most 64 megapixels and 64 MiB); the daemon decodes it, scales it to cover 768×1366, crops the center and stores the result, and a 768×1366 PNG is stored unchanged. Callers never send image bytes. |
| `SaveAppearance` | `(ssday) → (s)` | Trusted Settings only. Create (empty id) or update a user appearance: name (1 to 32 characters), hue 0 to 1 and a PNG or JPEG of any resolution (empty keeps the current image on update). Returns the id. At most 32 per user. |
| `DeleteAppearance` | `(s)` | Trusted Settings only. Remove one of the user's own appearances and its wallpaper file. |
| `Changed` | `(ss)` | Announce the new path and SHA-256 after a successful update. |

For the current root session, the service writes
`/var/openwave/wallpaper/<sha256>.png` and stores the selected hash in
`/var/openwave/wallpaper/current`. Other users have separate storage under
`/var/openwave/wallpaper/users/<uid>/`. Per-user directories belong to the broker
and have mode 0700. The service resolves non-root app identities through appd
and keys storage by their session user, rather than an isolated app's UID.
`SetWallpaper` requires verified trusted `com.openwave.settings`, or root. `ApplyAppearance` additionally allows the authenticated supervised Ocean process, so the Quick Menu can apply an installed appearance without being able to send arbitrary images. The supervised
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

Run the appearance end-to-end check, which builds nothing and needs a built daemon, with
`dbus-run-session -- python3 userspace/wallpaperagentd/tests/apply_appearance.py DAEMON LOADER LIBRARY_PATH`.

Run the writer-identity and appearance-lookup regression with
`python3 userspace/wallpaperagentd/tests/test_authorization.py`. Guest coverage
of the real Settings file/photo selection, crop and save flow is recorded in
[the recovery report](../../docs/PORTAL-WALLPAPER-OPENRC-RECOVERY.md).
