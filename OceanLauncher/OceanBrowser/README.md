# Ocean Browser profiles and tabs

The native engine is WPEWebKit 2.54 with JavaScriptCore. Its OpenWave build
profile lives in `cross/openwave-wpe-options.cmake`; it enables AVIF via
libavif/dav1d and video, Media Source, WebAudio, WebCodecs, MediaRecorder,
MediaStream, and WebRTC via GStreamer. The target plugin set includes MP4,
WebM, HLS/DASH, H.264/AAC, VP8/VP9, AV1, Opus, and Vorbis support.

From the repository root, `./build-openwave.sh browser-engine` builds the pinned
codec/media dependencies, rebuilds the engine, and installs into `rootfs`.
Use `deps`, `media`, or `engine` to resume those stages individually.
`OPENWAVE_BUILD_JOBS` controls parallelism (default 3). NASM and GLib generator
scripts run on the host; only target libraries and plugins belong in the image.
The script checks that the local LLVM compiler uses OpenWave's glibc TLS stack
guard before compiling media dependencies. LLVM's X86 backend and its
`stack-protector-target.ll` regression test cover this OS triple.

Image support follows the response bytes, not the URL extension. For example,
WallpaperCave's mobile `/mwp/` JPEG/PNG URLs can return `image/avif`, while its
desktop image variants remain JPEG/PNG. The previous AVIF-disabled engine
could therefore display desktop variants but fail to decode mobile variants.

On Virgl, the Skia image atlas uses OpenGL uploads: CPU writes into mappable
DMA-bufs were not visible in the host texture, leaving decoded static images
blank. This fallback preserves GPU tile painting and compositing and does not
change website styles. Other renderers retain the DMA-buf path when supported.
WebKit media buffering uses the profile's `cache/HTTP` directory rather than
assuming `/var/tmp` exists. The SCTP transport reads `max-channels` into a
`guint`, matching its GObject property type and avoiding a stack overwrite.

`tests/engine_fixture.py` serves local image and media fixtures for native QEMU
testing, including AVIF behind JPEG/PNG extensions, MP4 playback, WebM Media
Source playback, and a loopback WebRTC data channel. The host fixture generator
requires Pillow and GStreamer tools (including the Matroska plugin). Build the cross-compiled
`browser-engine-probe` target in the test CMake project, then run it inside the
QA VM with `http://10.0.2.2:8899/index.html 500`. The optional second argument
sets the CSS viewport width. Probe sessions are ephemeral and results appear
as `ENGINE_PROBE` JSON in the application output. Fixtures and the probe are
test tools and are not installed into the production browser profile.

The effective Unix username selects the browser profile at
`/Data/Users/<USER>/.DataLocation/Applications/browser`. The application ID is
`browser`, matching the installed desktop entry.

Profile layout:

| Path | Purpose |
| --- | --- |
| `tabs.json` | Regular tab URLs, titles, order, and selected tab when session saving is enabled |
| `Browser/Settings.json` | Atomic, private native Settings preferences |
| `Browser/History.sqlite` | SQLite visits with URL, title, favicon reference, and Unix millisecond timestamp |
| `Cache/Favicons/` | Reusable PNG favicons, independent of history records |
| `bookmarks.json` | Bookmark entry store, reserved for the bookmark UI |
| `Downloads/Downloads.sqlite` | Persistent normal download metadata and failure details |
| `WebEngine/` | Native WPE profile storage, including its engine-managed cookie store |
| `cache/images/<TAB-ID>.jpg` | Page snapshots, blurred by Qt Quick in the tab view |
| `cache/HTTP/` | WPE network, media, and website caches |
| `cache/GPUCache/` | Mesa shader cache location |
| `cache/JSCache/` | Reserved for an engine-supported disk bytecode cache |
| `passwords/` | Reserved for future protected credential storage |

WPE's JavaScriptCore diskCachePath setting belongs to the `jsc` command-line
runner, not WebKit. The browser does not pretend it enables a WebKit bytecode
cache. Passwords are not saved as plaintext files.

Each tab owns a separate live WPE view and navigation stack. Regular tabs share
one persistent network session. Private tabs share a separate ephemeral session
until the last private view closes; they do not enter history or session files,
and their snapshots stay in the browser's memory image provider.

`BrowserInput` attaches to each native WPE view. Every primary-button drag acts
as a touchscreen gesture, including mouse, USB tablet, stylus mouse emulation,
and synthesized events from touch or remote clients. No Shift key, kernel flag,
environment override, or device classification is required. Keyboard modifiers
do not switch dragging into text selection. Taps use WebKit's native
touch-to-click handling; wheel and secondary-button input retain their existing
behavior. Actual touch
points are forwarded with their individual press/move/release states, and a
lost grab, hidden tab, or canceled touch ends the gesture without leaving it
active. No JavaScript scrolling or overlay blocks web content.

A remote client must transmit a button-held drag or a scroll event. RealVNC's
mobile mouse interaction mode sends one-finger swipes as buttonless cursor
movement; those are indistinguishable from desktop mouse hover. Its mouse mode
uses two-finger scrolling, or double-tap-and-hold followed by movement for a
held-button drag. Touch panel mode is available on iOS and uses tap-and-hold
followed by dragging. See the [RealVNC gesture guide](https://help.realvnc.com/hc/en-us/articles/360018541231-Using-RealVNC-Viewer-for-Mobile-to-control-a-remote-device).
The browser does not turn ordinary cursor movement into scrolling.

The native input integration suite uses the real WPE engine under Ocean in an
isolated QEMU image; it covers dragging, taps, nested scrolling, wheel input,
modified and synthesized pointer drags, cancellation and actual touch events:

```bash
./build-openwave.sh test-browser-input out/OpenWave-input-test.img
```

Show all tabs collapses the menu into its bottom-right opening corner, captures
the active page, and shrinks
it into a two-column grid, with successive pairs of tabs scrolling vertically.
Page geometry and CSS viewport width stay
fixed throughout the animation. Tapping a card expands its preview and returns
to the same live view. Reduced-motion preferences disable transition durations.

Closing a card shrinks and fades it before removing its view and snapshot.
The overview then opens the remaining tab when only one is left; closing the
sole tab creates and opens a fresh `about:overview` tab.

The address bar's GPU-rendered loading outline uses a seamless conical gradient.
WPE load-started reveals it over 180 ms, with a 2.2-second color rotation;
succeeded, failed, and stopped statuses fade it away over 600 ms. Redirects
preserve the animation, and switching tabs resets it to the selected view's
navigation state. Hidden windows stop animation updates. Reduced-motion mode
uses a static outline while loading. The existing bar and its controls remain
unchanged and interactive.

Google search compatibility applies to every search section, including All,
Images, and Videos. A document stylesheet asks WPE to composite decoded images
that otherwise remain blank in the tiled renderer, covering both thumbnails
and site icons. Native construct-time user scripts install it in every tab on
each new document, including restored and private tabs. The stylesheet also
covers lazy-loaded and dynamically inserted results, without polling or
changing image URLs or loading policy. Other sites receive no stylesheet.

Preview files are replaced atomically, deleted when a tab closes, and pruned on
startup if they no longer belong to a restored regular tab.

`about:history` uses a native Qt Quick view in the tab's WebKit navigation stack.
Rows show only the favicon, title, and local date/time, grouped by date. Search
matches titles and stored URLs. The bottom hamburger offers Delete History,
Select, Sort, and View mode. Hold an entry to select it for individual deletion.
Deletion supports selected visits, today, yesterday, the last seven days, an
inclusive local date range, or all history. List and Compact modes share the
same typography and dividers.

OceanHistoryManager runs QSQLITE on a dedicated thread and pages results into
its Qt list model. WAL, foreign keys, secure deletion, and a 5-second busy timeout
are enabled. Only successful regular navigations are recorded; repeated visits
remain distinct. Internal pages, failures, redirects before the final page, and
private tabs do not create visits. Favicons are fetched asynchronously, decoded
with size limits, and stored outside SQLite. Missing or invalid icons use the
default webpage icon. Existing JSON visits migrate transactionally once, and the
old file is removed only after commit. Database errors leave Ocean running.

The target runtime needs Qt Sql's QSQLITE plugin and Qt Network's OpenSSL TLS
backend for HTTPS favicons. Enable Qt's `ssl`, `openssl`, and `openssl_linked`
features against the OpenWave sysroot's OpenSSL headers and libraries; stage
`libQt6Sql`, `libQt6Network`, `sqldrivers/libqsqlite.so`, and
`tls/libqopensslbackend.so`. The system CA bundle verifies HTTPS servers.

The shared application-data resolver reads the application ID from its installed
desktop entry and resolves the effective Unix user. Profile directories use
0700 and database/cache files use 0600. This isolates other Unix users; OpenWave
currently runs apps as root, so filesystem permissions cannot isolate one root
application from another. Per-application credentials or sandboxing are required
for that stronger boundary.

Downloads use WPEWebKit's native NetworkSession/WebKitDownload APIs, including
the requesting tab's authenticated cookies. QtWebEngine is not installed or
substituted for Ocean's existing engine. OceanDownloadManager exposes a Qt list
model and writes metadata through QSQLITE on a dedicated worker thread. Active
progress is sampled once per second; completion is reported only after the
file is flushed and finalized. Filenames are sanitized, and atomic no-overwrite
renames add numbered suffixes for collisions. Failed and cancelled transfers
never become completed files. Pending transfers from a previous process become
failed records. This WPE API supports cancellation, but not pause/resume, so
those controls are absent. Safe GET retries use the original live tab's engine.

Ask on Download defaults to enabled and opens the native save portal. With
it disabled, files and saved images use the chosen download folder, initially
the resolved user's Downloads directory. Explicit Save Image As, Save Link As,
and Save As always use the existing OpenWave save portal. The native engine first exports into private transfer storage,
then a worker writes through a duplicated writable PortalGrant descriptor. The
returned file path is display metadata only. Save As archives the page using
WebKit's MHTML export. Download list removal and Clear completed affect only
metadata, never the downloaded payload. Open File and Show in Files pass the
completed path to Ocean Files, whose existing profile-root checks still apply.
Private transfers stay in memory and disappear when the private session ends;
explicitly saved files remain at their destinations.

OceanCookieManager configures the profile before any WPE view is created.
Regular views use WebEngine/ for persistent native storage and cache/HTTP/ for
cache storage. WPE's public cookie API requires a storage filename; WebKit owns
its database format, values, parsing, expiration and security enforcement. The
old closed cookies/ directory migrates intact, including SQLite sidecars. The
native policy defaults to accepting first-party cookies and rejecting third-party
cookies; the Privacy switch updates all live sessions.
Persistent credential storage is disabled; no password manager is implemented.
Session cookies are not made persistent. Private views share an ephemeral
network session that is released when its final view closes. Storage errors
fall back to an ephemeral regular session rather than another disk location.
Cookie values are not exposed to QML; the native Privacy page provides only
the third-party acceptance policy switch.

Target backend tests are in tests/tst_downloads.cpp and build as
browser-download-tests when the test CMake project is cross-compiled. They
cover filename sanitation, schema migration, interrupted records, persistence,
missing files, removal without payload deletion, private storage permissions,
connection cleanup, and cookie-store migration and unsafe-path rejection.
The host fixture tests/download_cookie_fixture.py provides authenticated,
unknown-size, slow, interrupted, and ordinary downloads for QEMU, plus cookie
path/domain/expiry/HttpOnly/Secure probes. Observations contain cookie names
only, never cookie values. Test restarts and private-session isolation with
these fixtures through the actual browser, not a separate HTTP downloader.

## Native Settings

`BrowserSettingsPage.qml` is a separate Qt Quick overlay, opened from Settings
in the browser menu or `about:settings`. It keeps the current page behind a
GPU-blurred, rounded translucent panel; Back/Escape or its back button closes
it without navigating or discarding the current tab. General, Search and
Privacy use the browser's existing font and scaled phone canvas. Toggles are
Qt Quick Controls `Switch` instances with vector tracks and thumbs, keyboard
input and accessibility state, not image overlays.

`BrowserPreferences` uses the application-data resolver's directory and saves
validated preferences atomically with owner-only permissions. General controls
native save prompts, a system-portal-selected download folder, private startup,
regular-session restore and full versus origin-only address display. Editing
always shows the complete URL. Disabling session restore clears its saved tab
list immediately; private tabs remain excluded regardless of this preference.
Search chooses Google, DuckDuckGo or Bing for address-bar keyword searches.
Privacy controls future history recording and applies the native third-party
cookie policy to existing and new views. Previously recorded history and
cookies are not deleted when those switches change.

`browser-settings-tests` covers persistence, private file permissions, malformed
and unsafe storage, search URL encoding, and actual mouse/keyboard switch input.
The existing download/cookie regression tests use the same preferences backend.
