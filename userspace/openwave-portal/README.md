# OpenWave File Chooser Portal

`openwave-portal` owns `org.openwave.portal` on the system bus. Its native
interface is `org.openwave.portal.FileChooser` at
`/org/openwave/portal/FileChooser`. It does not own an XDG portal bus name.

Its interface contract is maintained in
`userspace/dbus/interfaces/org.openwave.portal.FileChooser.xml`, installed at
`/usr/share/dbus-1/interfaces/`. The broker uses the shared `OpenWave::Xml`
library to load this contract during startup. See [the XML library](../xml/README.md).

OpenWave currently has one Wayland compositor, Ocean. The
`PortalFileChooserBackend` in Ocean is the `openwave-portal-filechooser`
provider: it presents the shared `PortalFileChooser.qml` modal above the
requesting app and uses Ocean Files' `FilesBackend` to browse the current
user's `/Data/Users/<username>` tree. This keeps the picker out of Settings
without launching a second Ocean Files window through the normal app launcher.

| Method | Input | Result |
| --- | --- | --- |
| `OpenFile`, `OpenFiles`, `SaveFile`, `SelectDirectory` | `a{sv}` options | request object path |
| `GetStatus` | request path | status and error string |
| `CancelRequest` | request path | cancels that caller's request |
| `TakeResult` | request path | status, display names, Unix FD handles |
| `RegisterBackend`, `NextRequest`, `CompleteRequest` | backend protocol | restricted to the authenticated Ocean process |
| `ListGallery`, `OpenGalleryItem`, `CompleteGalleryRequest` | request-bound gallery tokens | restricted to the authenticated Ocean process |

Statuses are 0 pending, 1 selected, 2 cancelled, and 3 failed. `Response`
signals notify the requester; the Qt client also polls to recover if a signal
is missed. The broker serializes requests. It fails pending work if Ocean
disconnects and drops requests when their application disconnects.

Options include `title`, optional translated `muted_title` and `primary_title`
segments for the shared two-tone portal heading, `mime_types` (`as`, including `image/*`), `multiple` (`b`),
`initial_directory`, `suggested_filename`, `parent_window`, and
`request_identifier`. The broker derives `application_id` from D-Bus process
credentials and the protected appd registry; caller-supplied identity is not
trusted. `parent_window` is retained as a hint. Ocean currently has one phone
viewport, so its picker is presented as a global modal over the active app.

`openPhoto()` and `openPhotos()` set the image filter and expose Files/Photos
sources through `image_sources` (`b`) and `image_source` (`files` or `gallery`).
The broker lists only Pictures and the session user's imported Photos library.
Gallery entries have request-bound opaque tokens; previews and selection reopen
the image without following symlinks and reject replaced inodes. Enumeration is
bounded to 4096 entries, depth six, and 1000 images of at most 64 MiB each.
Listing and preview check the live permission lease without consuming it. Final
descriptor delivery consumes it; cancellation releases it. App identity, lease
expiry and revocation are checked throughout.

Ocean renders headings with `PortalTitle.qml`. New portal dialogs, including
future permission and authentication prompts, should reuse it with translated
`mutedTitle` and `primaryTitle` strings. The title uses a continuous opacity
gradient, reaching full emphasis at the primary segment. Legacy `title`
strings use their final whitespace-separated word as the primary segment;
a single-word title remains fully bright.

Only canonical resources below the authenticated user's data root can be
selected. The broker checks file type and requested MIME type, then uses
Linux `openat2` with `RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS` to open the
chosen resource, and returns a descriptor to the original requesting D-Bus
connection. It does not return unrestricted filesystem paths. Open grants are
read-only; save grants create one file for writing; directory grants are
directory descriptors. Applications receive no authority over other paths.
Save/Create grants use exclusive creation with mode `0600` under the verified
session user's filesystem UID and primary GID. Downloads therefore belong to
the user, while the requesting app writes only through its granted descriptor.
The broker drops supplementary groups before starting worker threads. D-Bus
may relay the writable descriptor, but cannot open user paths or create files.

Qt Quick applications link `userspace/portal-client/OpenWavePortals.{h,cpp}`
and expose one `OpenWavePortals` object. `openFile(options)` and the other
methods emit `completed(grants)`, `cancelled()`, or `errorOccurred(message)`.
Each grant has a display name and scoped descriptor. Its `temporaryCopy()`
method makes a private, short-lived local copy for QML components that require
a URL, such as the existing wallpaper cropper. C++ consumers can use the
descriptor directly. The copy is removed when its grant is destroyed.

Run the isolated IPC test with:

```sh
python3 userspace/openwave-portal/tests/run_contract.py
```

This builds a temporary test-only broker against the target SDK and runs it on
a private D-Bus session. Production brokers reject test-root arguments. The
enforcing guest Wallpaper workflow is documented in
[the recovery report](../../docs/PORTAL-WALLPAPER-OPENRC-RECOVERY.md).
