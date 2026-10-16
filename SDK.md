# OpenWave SDK and runtime filesystem

Development and deployment are separate. The existing modified LLVM compiler and
Qt 6.11.2 configuration are preserved. No application UI was redesigned.

```text
toolchain/
  bin/                           all Clang/LLVM and Qt development executables
  include/                       LLVM, host Qt and OpenWave public headers
  lib/                           host LLVM/Qt libraries and compiler runtimes
  lib/cmake/OpenWaveSDK/          public API package and target-specific exports
  sysroot/<target-triple>/        target Qt, dependencies and API client libraries
rootfs/                         deployable runtime files only
out/staging/<target-triple>/userspace/  build installation staging
```

The current target is `x86_64-unknown-openwave`. ARM64 has separate SDK prefixes
and build directories, but its actual sysroot, target Qt and runtime still need
an ARM64 port. See [the SDK guide](toolchain/README.md) for API components and
external application builds.

## Build and deploy

For a Qt application, source the repository environment before configuring a
fresh build directory. It selects the exported Clang, target Qt/API packages,
native Qt tools and target pkg-config directories:

```sh
source env.sh
cmake -S /path/to/application -B /path/to/build -G Ninja
cmake --build /path/to/build
```

Existing CMake build directories retain their cached compiler/toolchain; use a
fresh directory when switching from another SDK. Repository build scripts detect
stale SDK paths and refresh their CMake configuration automatically. `qmake` on this environment's
PATH is the SDK's OpenWave target wrapper.

```sh
./build-openwave all
./build-openwave all --image out/OpenWave-new.img
./build-openwave userspace
./build-openwave userspace --image out/OpenWave-new.img
./run-openwave --image out/OpenWave-new.img run
```

`all` applies the OpenWave LLVM patch, rebuilds and installs Clang/LLVM with its
existing configuration, then builds the available dependency groups, kernel,
browser engine, SDK clients and system applications. It requires the existing
target SDK and source inputs described in `README.md`. `./build-openwave llvm`
builds only the compiler; `./build-openwave llvm-patch` prepares the sources.
Ordinary application builds and SDK export prepare the patch automatically but
reuse the installed compiler.

The QEMU launcher disables virtual Ethernet, so guest internet access requires
connecting Wi-Fi through the passed-through USB adapter. By default it serves
VNC on the host's primary IPv4 address at port 5900, using EGL for accelerated
VirGL rendering. Connect a VNC client to that address to view OpenWave. Use
`--window` for the local GTK/SDL OpenGL window instead, or use `--vnc` with
`--vnc-host` and `--vnc-port` to choose another listening address and TCP port.
For example, `./run-openwave --vnc --vnc-port 5901 run` serves VNC on port 5901.
The window and VNC mode flags are mutually exclusive; the older
`--vnc-host none` window selection remains supported.

The script builds SDK client libraries, compiles the daemons and apps with the
exported compiler/sysroot and host Qt tools, installs them into runtime staging,
then assembles `rootfs/`. A new image is only published after `e2fsck` passes; an
existing image is never overwritten. `fakeroot` remains a host metadata command
for image ownership and is not the build sysroot or runtime directory.

`./build-openwave sdk` rebuilds only the public clients. The dependency build
helpers install full development dependencies into the architecture-specific
SDK sysroot, then run the runtime assembler. They use new build caches under
`out/sdk-dependencies/<target-triple>`; the existing engine and codec binaries
were exported intact, without an unnecessary WPE or compiler rebuild.

`./build-openwave selinux` downloads the checksum-pinned
[SELinux 3.11 libraries](https://github.com/SELinuxProject/selinux/wiki/Releases),
builds libsepol (including CIL) and libselinux using the exported Clang/sysroot,
and assembles rootfs. This step also runs automatically in `all`. It requires
host `flex` and target PCRE2. Headers (`usr/include/selinux` and `usr/include/sepol`),
static/shared libraries and pkg-config files go into the target SDK sysroot;
only `usr/lib64/libselinux.so.1`, `usr/lib64/libsepol.so.2`, their runtime
dependencies and licenses are deployed. Python/Ruby bindings and upstream target
policy utilities are not built. Native clients can use target pkg-config packages `libselinux`
and `libsepol` after sourcing `env.sh`. The build also stages OpenWave's policy;
the userspace build integrates early policy loading/labeling and app confinement.
See [SELinux configuration and its current boundaries](docs/SELINUX.md).

The assembler copies installed runtime binaries, configuration, fonts, licenses,
locale/time-zone data (including ICU’s external `.dat` resource) and app resources. It selects the deployed Qt QML modules
and plugins, follows ELF shared-library dependencies, includes dynamically loaded
NSS/graphics/plugin dependencies, and rejects headers, static libraries, package
metadata and development RPATHs. Legacy RPATHs are cleaned in deployed copies
using host `patchelf` (included under `toolchain/bin`). Rootfs has no links into
the development SDK.

`out/rootfs-manifest.json` reports size, checked ELF files, selected Qt modules
and any RPATH cleanup. Assembly uses a temporary tree and retains the previous
rootfs as `out/rootfs-previous` until the next successful assembly. Existing
runtime state in the baseline tree is preserved. Applications use the same
`/usr`, `/etc`, `/var/openwave`, `/var/lib/openwave`, `/Data/Users/...` and `/tmp`
paths as before.

## Original build preservation

The original mixed installation is retained in `out/legacy-fakeroot`; the
validated build no longer uses it. Clang, host/target Qt and the public API now
share the same `toolchain/bin`, `include` and `lib` prefix. The old vendor folders
have been removed; the repository `qt-host/` path links to `toolchain/`.
Target runtime libraries live in architecture-specific sysroots. Obsolete
Qt/WebKit build trees and test images
have been cleaned; their build configurations and loose QA evidence are archived
under `out/cleanup/`. The old `OpenWave.img` is
preserved; test the new runtime image with the explicit `--image` argument above.

Preview a cleanup with `python3 scripts/clean-openwave-out.py`, then use
`--apply` to remove known obsolete outputs. The helper keeps final images,
current SDK/app build caches, the kernel/compiler builds and host utilities,
checks for mounted directories and live process references, and archives loose
evidence/configurations before removal. Installed SDK links into candidate
directories prevent cleanup.

Export or recreate the complete shared SDK, including the public API:

```sh
python3 scripts/export-openwave-sdk.py --source out/legacy-fakeroot
```

The exporter works when `toolchain/` is absent. It installs the surviving
`out/llvm-build` compiler without rebuilding LLVM, restores compiler runtimes,
and exports the existing target Qt/dependency installation. If host Qt was also
deleted, it builds matching Qt from `third_party/qt6` with the original native
configuration in `cross/qt-host-sdk.cmake`; that cache is kept in
`out/sdk-host-qt`. Existing host Qt is reused. Finally, it automatically builds
and installs OpenWave's public API libraries and headers in the shared prefix.
It relocates Qt build metadata and target wrappers. The SDK can be copied
independently; CMake paths resolve relative to its toolchain file. Meson machine
files use absolute paths, so rerun `toolchain/cmake/generate-meson.py` after moving
it. qmake target wrappers also use the exported compiler and sysroot.

Re-exporting uses the installed `toolchain/bin` tools by default. To import
another existing host Qt installation, pass `--host-source /path/to/host-qt`.
For an installed modified compiler, pass `--compiler-source /path/to/llvm`.
The host Qt version must match target Qt; an older system installation is not
silently substituted. Use `--jobs` to control host Qt/API build concurrency.
After rebuilding modified LLVM, install it into the canonical compiler prefix:

```sh
cmake --install out/llvm-build --prefix "$PWD/toolchain"
```

Public service implementations remain in `userspace/` and the Shell. Apps link
`OpenWave::Prompts`, `OpenWave::FileChooser`, `OpenWave::Applications`,
`OpenWave::Wallpaper`, `OpenWave::Time`, `OpenWave::Connectivity`,
`OpenWave::SystemControls`, `OpenWave::ApplicationData`, and/or `OpenWave::Xml`. This exports only
working interfaces; it adds no placeholder services or promises of unrestricted
application launch or file access.

`OpenWave::Fonts` adds the central Source Sans Pro installation and the
`OpenWave.Fonts` Qt Quick module. Initialize `OpenWave::FontManager` after
creating `QGuiApplication`, then use `Fonts.family` or `Fonts.font()` in QML.
See [the font API guide](userspace/fonts/README.md) for the catalog, standard Qt
font properties, and SDK import paths. The SDK exports type metadata and a
shared plugin; rootfs receives the plugin, shared library, fonts and license.

`OpenWave::Xml` exposes a C/C++ XML loader backed by the existing target libxml2.
Daemon D-Bus contracts live in `userspace/dbus/interfaces/` and are installed
under `/usr/share/dbus-1/interfaces/`; they are loaded during daemon startup.
See [the XML API](userspace/xml/README.md) for ownership and parsing behavior.

## Validation

The x86_64 userspace build completes against the exported SDK. An external Qt
Quick service client builds with a copied SDK at a different path, and a qmake
consumer also compiles using its exported compiler/sysroot. The generated ELF
RPATHs use on-device library locations, with no SDK or build directory paths.

QEMU checks cover Shell startup, Settings, the native prompt/chooser clients,
wallpaper and time services, and launching/terminating Files and Browser through
the exported lifecycle API. Browser checks include `about:overview`, PNG, JPEG,
GIF, WebP, AVIF, animated AVIF and AVIF with JPEG/PNG URLs, plus video playback and HTTPS navigation. The logs, screenshots and validation
report are saved in `out/sdk-validation/`.
Qt's Fusion style, XKB keyboard data and ICU runtime data are retained because
these are required by the existing apps; their development utilities are omitted.

The validation image is `out/OpenWave-sdk-final.img`:

```sh
./run-openwave --image out/OpenWave-sdk-final.img run
```

The runtime tree is approximately 481 MiB, compared with approximately 817 MiB
for the original mixed installation. `out/rootfs-manifest.json` records the exact
size and ELF dependency checks. ARM64 remains unbuilt and untested.

## Application management

`OpenWave::Applications` now provides `ApplicationsClient` for appd registry and
lifecycle operations. The existing `OpenWaveApplications::launch()` API routes
through the versioned system-bus service; Ocean retains visual handoffs and
Wayland surfaces, while appd owns processes. See
[appd architecture and security limitations](userspace/appd/README.md).

### Permissions

Use `find_package(OpenWaveSDK REQUIRED COMPONENTS Permissions)` and link
`OpenWave::Permissions`. `<OpenWave/PermissionClient.h>` provides the asynchronous
`OpenWave::PermissionClient` Qt model/API for permission requests, status, scoped
listing, and change notifications. Settings administrative methods are rejected
for other application identities. Supported identifiers are `files.read`,
`files.write`, and `files.directory`; other resources are restricted.
The read-only `records` property exposes the current model's dictionaries to
QML and notifies through `recordsChanged`. Records include `requestedAt` (Unix
milliseconds, zero if no request was recorded) and `supported`. Status queries
and administrator policy changes never fabricate request history. Cross-app
records remain restricted to authenticated Settings.

Permission status alone does not grant filesystem access. Use
`OpenWavePortals::openFile`, `saveFile`, or `selectDirectory` for brokered grants.
Directory grants expose `directoryToken`; `createFile(directoryToken, filename)`
creates a non-overwriting file in that selected folder. An empty directory token
uses the current user's Downloads folder. Writes use `PortalGrant::writeAll` or
the granted descriptor, not the display path.

Applications must be installed and launched through appd. Settings and Files are
trusted system components; regular applications use isolated UIDs and inherited
Landlock restrictions. Build the corresponding kernel with
`./build-openwave kernel`. See [permissiond](userspace/permissiond/README.md) for
policy lifetimes, enforcement, revocation, and validation commands.

## Native bundles

See [the bundle contract](docs/APPLICATION-BUNDLES.md) for manifest validation, SDK CMake installation, runtime search paths, full application IDs, and private Config/Cache/Data resolvers.

Application Services combines the existing native application, permission,
portal and prompt clients under `OpenWave::ApplicationServices` and
`<OpenWave/ApplicationServices.h>`. The daemons remain separate. See
[Application Services](userspace/application-services/README.md) for private
lifecycle IPC, capability availability, launch-bound grants and enforcement.
