# Website apps

In Ocean Browser, open a website and choose **More → Install as web app** from
the hamburger menu. The native promptd text-input prompt prefills the page title,
shows the captured address as read-only message text, and offers Cancel/Create.
The URL is captured before the prompt opens. Navigation or tab changes cannot
replace it while the name is edited. Internal pages and addresses containing
credentials are unavailable for installation.

Creation registers a regular application bundle under `/Applications/` and the
launcher updates through appd's existing registry notifications. Each bundle has
a stable URL-derived ID, an editable display name, the website icon, and
a small copy of the installed `openwave-webapp` executable. Qt and WPE libraries
remain shared system runtime dependencies. Installing the same URL twice returns
an existing-app error; names are metadata and never become shell commands or paths.

The Shell reserves its status bar above website windows, including older installed
shortcuts. The page starts at the bar's bottom edge. A small rendered edge sample
updates its background four times per second while a web app is foreground and
visible; a median rejects small header glyphs. Foreground icons switch between
dark and light using relative luminance. Sampling stops during transitions, when
hidden, and for other apps. Website controls are not shifted using injected CSS.
The quick-menu chevron stays above the status-bar background and uses black on
light pages and white on dark pages. Website apps use the Browser's mobile
viewport sizing, capped at 500 CSS pixels, so content remains readable while
the page fills the space below the bar.

Icon discovery checks favicon links, Apple touch icons, and web manifest icons.
Asynchronous, bounded requests select the largest decodable image, including ICO
frames, and normalize it to a PNG up to 512 pixels without upscaling small icons.
Unavailable or corrupt icons retain the default webpage icon. Requests are timed
out, credential URLs and local paths are rejected, and appd validates PNG data
before writing it. Existing shortcuts with a generic icon refresh it on their
first successful load with the updated host. Their URL, name, and data survive.

`openwave-webapp` embeds `OpenWave.Web 1.0` in its own Qt Quick window. System Back
navigates page history, then minimizes to home at the root. The existing lifecycle
channel handles graceful termination. Each app retains its own cookies and
website storage in its application data directory under `Data/WebKit`, with
caches under `Cache/WebKit`. Existing OpenWave data resolvers determine the user
and application paths. Browser cookies, history, and permissions are not imported.
Website content loads from the saved network address.

The Browser remains isolated. `ApplicationsClient::createWebApp(name, url, callback)`
calls `org.openwave.Applications1.CreateWebApp`. The overload with PNG bytes uses
`CreateWebAppWithIcon`; the original method remains compatible. A verified web
app leader may call `UpdateWebAppIcon` for its own bundle only. This method takes
no application ID or destination path, uses atomic writes, and notifies the Shell
with a new content-addressed icon path. appd authenticates
D-Bus credentials and permits only the leader of the installed, validated Ocean
Browser bundle. Self-declared IDs, root callers outside appd, other apps, and
Browser helper processes cannot install shortcuts through this method.

The broker accepts bounded HTTP/HTTPS addresses and names. It copies only the
fixed system runtime and either the validated icon or the default icon, writes a standard sandboxed manifest with no
permission grants, restores SELinux labels, and publishes the complete bundle
with an atomic non-replacing rename. Failed creation removes its staging tree;
Cancel does not call the installer. The existing appd UID, Bubblewrap, Landlock,
SELinux, cgroup, and seccomp isolation applies when a shortcut launches.

`./build-openwave userspace` builds/stages the Browser, appd method, D-Bus contract,
public C++ client, and runtime template. The template lives in
`/usr/libexec/openwave-webapp` and its icon in
`/usr/share/pixmaps/openwave-webapp.svg`. It is not independently listed in the
launcher; the generated bundles are the launchable applications.

`tests/tst_definition.cpp` checks URL rejection, stable bounded identities,
name validation, title suggestions, and metadata serialization without command
interpolation. Build `tests/` with a native Qt Core/Test installation to run it.
The target-only `webapp-runtime-tests` serves a local fixture while a QEMU/VNC
operator creates a shortcut through the real Browser UI. It checks authenticated
installation, chosen name and URL, registry updates, actual WPE rendering,
Browser storage separation, persistent cookies/local storage, SELinux domain,
and recovery after appd restarts. It shuts down the test VM after reporting.
Use it only in a disposable test image with the normal OpenWave init as PID 1.

The runtime design was informed by KDE/Angelfish commit
`2a82873b85f8b608c3b4c8cbe6244e9bc1396027`, particularly
`src/webappmanager.cpp`, `src/webappcreator.cpp`, and `angelfish-webapp/main.cpp`, for the small-runtime plus
metadata approach. OpenWave uses its own bundles, native prompts, and WPE engine;
no upstream implementation was copied.

The maximized-window offset was checked against Qt/qtwayland commit
`512ab34ec219a21e09b6988b2f87e3708d853482`,
`src/compositor/extensions/qwaylandxdgshellintegration.cpp`. Qt restores a
maximized move item's output position, so the Shell keeps the reserved web app
inset in a separate translation that also maps pointer and touch coordinates.

Uninstall website apps under **Settings → Apps → [website app] → Uninstall**.
The native prompt offers Cancel/Uninstall and explains that cookies, website
storage, settings, and cache will be deleted. Shared user files, including saved
downloads, are retained. System applications do not expose this action.

`ApplicationsClient::uninstallWebApp(id, callback)` calls `UninstallWebApp`.
appd authenticates the installed Settings leader before accepting an ID, validates
the website bundle, prevents relaunch, and waits for leaders and helper cgroups
to stop. A worker deletes only the validated bundle and the current user's
private application directory. Descriptor-relative traversal rejects mounted
subtrees and never follows links into user folders. Protected `.uninstall-ID`
directories prevent reinstall during cleanup and let interrupted deletions resume
when appd restarts. The existing ApplicationRemoved event invalidates brokered
access and removes permission decisions. Reserved application UID mappings remain
system metadata, preventing accidental UID reuse; they contain no website data.
The exported SDK requires no new transport or application-side filesystem access.

The uninstall design was informed by KDE/Angelfish commit
`2a82873b85f8b608c3b4c8cbe6244e9bc1396027`, `src/webappmanager.cpp`, for removing
the launcher registration and publishing a model update. OpenWave implements its
own authenticated broker, process shutdown, and private-storage deletion.
