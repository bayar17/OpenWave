# OpenWave native SDK

The SDK is independent of the deployable `rootfs/`. Copy this whole directory to
relocate it. Host development programs run on Linux x86_64; target libraries are
isolated by OpenWave target triple. The modified Clang/LLVM installation lives
directly in the shared prefix; it has been moved intact, without rebuilding it.

```text
toolchain/
  bin/                        Clang, LLVM, Qt tools, moc, rcc, qsb and patchelf
  include/                    LLVM, host Qt and OpenWave public headers
  lib/                        host LLVM/Qt libraries and compiler runtimes
  lib/cmake/OpenWaveSDK/       public API CMake package
  sysroot/<target-triple>/     target Qt, system and OpenWave client libraries
  cmake/                      target toolchain files
```

All development executables live directly in `bin/`. There are no separate
`clang/`, `qt/` or `openwave-api/` installations. `libexec` is a compatibility link
to `bin` for Qt's generated metadata. The repository's former `qt-host/` path
links to this shared prefix. Target libraries remain in the target sysroot so
host tools use the host Qt configuration and apps use OpenWave's Qt configuration.
The SDK has no links to development files outside this directory.

- `bin/`: modified Clang/LLVM and host Qt tools, including moc, rcc, qsb and
  qtwaylandscanner. `qmake` and `qt-cmake` compile OpenWave applications;
  `qmake-host6` retains the native host Qt configuration.
- `include/OpenWave/`: public C++ service client headers.
- `lib/cmake/OpenWaveSDK/`: relocatable API package and target-specific exports.
- `sysroot/x86_64-unknown-openwave/`: target C/C++ runtimes, dependency headers,
  libraries and package metadata, including the existing Qt 6.11.2 target
  installation in `usr/` and OpenWave API libraries in `usr/lib64/`.
  This is a compilation sysroot, never an image staging tree.

Compile an application with CMake and Ninja installed on the development host:

```sh
cmake -S /path/to/application -B /path/to/build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain/cmake/openwave-x86_64.cmake \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build /path/to/build
DESTDIR=/path/to/runtime-staging cmake --install /path/to/build
```

An application's CMakeLists can use:

```cmake
find_package(Qt6 6.10 REQUIRED COMPONENTS Quick)
find_package(OpenWaveSDK REQUIRED COMPONENTS Prompts FileChooser Applications)
qt_standard_project_setup(REQUIRES 6.10)
qt_add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE Qt6::Quick
    OpenWave::Prompts OpenWave::FileChooser OpenWave::Applications)
set_target_properties(myapp PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../lib;$ORIGIN/../lib64")
```

Use headers such as `<OpenWave/OpenWavePromptClient.h>`. Available components:

| Component | Public client | Runtime service |
|---|---|---|
| Prompts | OpenWavePromptClient | promptd; Shell renders prompts |
| FileChooser | OpenWavePortals, PortalGrant | openwave-portal; Shell renders chooser |
| Applications | OpenWaveApplications, ApplicationChannel | Ocean Launcher and per-process channels |
| Wallpaper | WallpaperClient | wallpaperagentd |
| Time | TimeBackend | timed |
| Connectivity | ConnectivityBackend | connectivityd |
| SystemControls | QuickMenuBackend | audiod, connectivityd, backlight devices |
| ApplicationData | application-data.h, preferences.h | Existing per-user data conventions |
| Xml | OpenWaveXml.h | Local XML parsing with libxml2; external D-Bus interface files |
| Fonts | FontManager.h; `OpenWave.Fonts` QML module | Shared system fonts and Qt's font database |
| Web | WebRuntime.h, WebInput.h; `OpenWave.Web` QML module | Existing WPE WebKit engine, embedded in the app |

These are the existing asynchronous clients, not mock services. File chooser
results grant file descriptors; callers should use PortalGrant rather than
assuming arbitrary filesystem access. Application launching accepts the installed
launcher app IDs; the launcher currently manages Files, Browser and Settings.
Graceful termination uses `request(instanceAddress(pid), {{"command", "quit"}}, ...)`
for applications that implement that channel. This does not claim a general
installation or permissions API. The process renderer API is reserved for the
supervised Shell and retains its existing authorization checks.

`OpenWavePromptClient::passwordInput(title, body, options)` requests a masked
native prompt with a small Show/Hide password control at the bottom left.
It accepts the usual text-input bounds and labels and returns through `finished`.
Set `allowPasswordReveal=false` to disable revealing. `ConnectivityBackend`
also exposes `forgetWifi(ssid)` for removing a saved Wi-Fi profile; applications
should obtain confirmation through `promptd` before calling it.

The OS installs the corresponding `libopenwave-*.so.0` libraries in `/usr/lib64`.
SDK include paths are never runtime paths. Services continue using their existing
D-Bus policies, ownership checks and on-device directories. UI resources and
animations have not changed.
The CMake package exposes the public headers through a relative `OpenWave` link
in the target sysroot's include directory. This keeps host Qt headers out of
target applications' include search paths while retaining one public header copy.

For Meson, regenerate machine files after moving the SDK:

```sh
python3 /path/to/toolchain/cmake/generate-meson.py
meson setup build source --cross-file /path/to/toolchain/cmake/openwave-x86_64.ini
```

ARM64 needs its own populated `sysroot/aarch64-unknown-openwave`, target Qt and API
libraries. The export script accepts `--target aarch64-unknown-openwave --source
/path/to/arm64-installation` and generates a separate CMake/Meson profile. It does
not manufacture an ARM64 sysroot from x86_64 files. ARM64 is not yet built or tested.

SDK sources and the compiling example are in `userspace/sdk/` in the OpenWave
repository. SDK package metadata and headers are relocatable; re-exporting preserves
the original compiler and target Qt builds.

Recreate the complete SDK, including the public API libraries, from the repository:

```sh
python3 scripts/export-openwave-sdk.py
```

This works even after deleting `toolchain/`. The exporter reinstalls the modified
compiler from `out/llvm-build` without rebuilding LLVM, restores compiler runtimes,
exports target dependencies from `out/legacy-fakeroot`, and builds/installs the API.
It reuses installed host Qt tools. If those were deleted, it recovers matching
Qt 6.11.2 from `third_party/qt6` using the original native feature configuration;
the reusable recovery build stays in `out/sdk-host-qt`. This first recovery takes
longer than copying an existing installation. Keep these source installations
and build caches if you want to recreate a deleted SDK.

To import existing installations instead, pass `--compiler-source /path/to/llvm`
and/or `--host-source /path/to/native-qt`. Host Qt must match the target Qt version.
`--compiler-build`, `--qt-source`, `--host-build`, `--source` and `--jobs` override
the recovery inputs. A failed stage stops export; success is reported only after
the OpenWave API has built and installed into this same shared prefix.
Host Qt recovery requires native development dependencies (including PNG,
OpenGL/EGL, GBM, DRM and zlib), CMake, Ninja and a native C++ compiler. A locally
staged native dependency prefix can be supplied with `--host-dependencies`;
OpenWave target libraries are never used to build host tools.

The Fonts component uses Source Sans Pro from the central system font directory.
Call `OpenWave::FontManager::initialize()` after constructing `QGuiApplication`,
link `OpenWave::Fonts`, and import `OpenWave.Fonts 1.0` in QML. Use
`font.family: Fonts.family` or `font: Fonts.font(28, Font.Light)`.
`Fonts.families` and `Fonts.styles(family)` expose Qt's live font catalog.
For embedded web content, link `OpenWave::Web` and import `OpenWave.Web 1.0`.
Call `WebRuntime::configureGraphics()` before creating Qt Quick windows, then
`WebRuntime::initialize(&engine)` before loading QML. See
[`userspace/web`](../web/README.md) for usage, private-session defaults, and
a standalone SDK example. This module is separate from the desktop Settings portal.
Pass `OpenWaveSDK_QML_IMPORT_PATH` as the `IMPORT_PATH` argument to
`qt_add_qml_module` so QML tooling can resolve the installed module.

`OpenWave::Permissions` exports `OpenWave::PermissionClient`, an asynchronous
permission API and Qt model. File Chooser grants remain the enforcement path;
a permission check by itself conveys no ambient access. See
[permissiond](../permissiond/README.md) for supported permissions and trust rules.

`ApplicationsClient::createWebApp(name, url, callback)` installs an isolated
website shortcut through appd. The operation is restricted to the authenticated
leader of the installed Ocean Browser; other clients receive an authorization
error. See [website apps](../webapp/README.md) for the native prompt workflow,
bundle format, shared WPE host, and independent persistent website storage.
