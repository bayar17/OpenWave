# OpenWave

OpenWave is a Linux phone operating system with the Ocean Qt Quick shell and
system applications. The current target is `x86_64-unknown-openwave` in QEMU.

## LLVM sources

`llvm-project/` is an upstream Git submodule pinned to the base of the OpenWave
compiler patch. Initialize it after cloning this repository:

```bash
git submodule update --init llvm-project
git -C llvm-project apply --check ../patches/llvm/openwave-llvm.patch
git -C llvm-project apply --whitespace=nowarn ../patches/llvm/openwave-llvm.patch
```

The existing development checkout already has that patch applied. Submodule
initialization alone does not apply it in a fresh checkout. Keep OpenWave compiler
changes in `patches/llvm/`; see its README for validation and known limitations.

## Build commands

Project shell build, staging, image, and QEMU test commands share one entry point:

```bash
./build-openwave --help
./build-openwave sdk
./build-openwave userspace
./build-openwave userspace --image out/OpenWave-new.img
```

To pack an already assembled rootfs without rebuilding:

```bash
./build-openwave image rootfs out/OpenWave.img
```

Image creation refuses to overwrite existing images. The `audio`,
`bluetooth-audio`, `browser-engine`, `mdns`, and `tzdata` commands retain their
existing dependency builds. Automated guest checks use `test-appd` or
`test-browser-input` followed by a new test image path.

`env.sh` remains a separate, sourceable Qt development environment:

```bash
source env.sh
```

`run-openwave` remains the separate VM launcher:

```bash
./run-openwave --window --image out/OpenWave-new.img run
./run-openwave --vnc --vnc-port 5901 --image out/OpenWave-new.img run
```

`--window` (also `--qemu-window`) selects the local GTK/SDL OpenGL window and
disables VNC. `--vnc` selects the EGL-backed VNC server. VNC is the default;
its host address defaults to the host's primary IPv4 address. Use `--vnc-host
127.0.0.1` for a local client, or another host IPv4 address as needed.
`--vnc-port` takes the TCP port (5900–65535), not the VNC display number.
Window and VNC flags are mutually exclusive. Use `check` instead of `run` to
validate the selected configuration without starting the VM.

## Third-party dependencies

The consolidated builder has commands that fetch pinned sources, check their
commit or archive checksum, build them, and install into the target SDK:

| Command | Downloaded and built components |
| --- | --- |
| `./build-openwave audio` | ALSA, PipeWire, WirePlumber |
| `./build-openwave mdns` | libdaemon, Avahi |
| `./build-openwave browser-engine deps` | Host NASM; target dav1d, libavif, libvpx, GStreamer and its enabled subprojects |
| `./build-openwave browser-engine media` | GStreamer and its enabled media subprojects |

These commands require a populated OpenWave SDK and the host build tools used
by their packages. They do not bootstrap every dependency from an empty clone.
`bluetooth-audio` expects SBC, BlueZ, and PipeWire sources; `userspace` expects
mpg123; `tzdata` expects the bundled IANA time-zone sources. Building the WPE
engine expects `OceanLauncher/OceanBrowser/WPEWebKit`. SDK export reuses compiler,
Qt, and dependency installations/build inputs as documented in `SDK.md`.

The build helpers use the same SDK paths through `source env.sh --build`, without
adding target compiler flags to host-tool builds. Export the complete toolchain
with `python3 scripts/export-openwave-sdk.py`. See `SDK.md` for SDK configuration,
runtime staging, and rootfs separation.
