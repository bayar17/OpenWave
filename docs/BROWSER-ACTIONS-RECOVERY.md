# Browser downloads and web-app installation

The reported installation popup was `Cannot spawn a message bus when AT_SECURE
is set`. Ordinary Browser processes deliberately enter `openwave_app_t` with
secure execution enabled. GLib ignores `DBUS_SESSION_BUS_ADDRESS` in that mode;
the app's private runtime directory has no session bus to discover. The prompt
client now connects explicitly to appd's protected `/run/openwave/session-bus`
socket when `AT_SECURE` is set. Its supervised prompt-daemon identity checks
remain in place. Ordinary apps receive no `noatsecure` exception.
The portal and prompt brokers can now inspect a confined caller's executable
link in procfs, after appd resolves its live identity. Their previous metadata
rules covered trusted service domains only. The kernel's readlink check also
requires `file:read` against the caller's task SID. Opening proc files and
process tracing remain denied.

Download saves also needed broker creation rights and writable descriptor
relay rights. The portal now creates the default Downloads directory as the
verified session user and creates new files exclusively with mode `0600`.
It switches only the synchronous thread's filesystem UID/GID and restores
them before returning. Supplementary groups are dropped at daemon startup.
Existing files are never overwritten, symlinks cannot redirect the default
directory, and `openat2` keeps selected paths below the verified user root.
The Browser writes through its scoped descriptor; D-Bus may relay that
descriptor without gaining permission to open paths or create files. The
Shell's picker remains read-only.

User-installed bundles under `/Applications` have separate
`openwave_user_bundle_t` and `openwave_user_app_exec_t` labels. Appd can create
and atomically publish a bundle there. System runtime files and system app
binaries retain their existing immutable labels. Only the verified managed
Ocean Browser leader can request installation. Installed web apps still enter
the ordinary app domain, with private profiles, namespaces, Landlock, seccomp,
no effective capabilities and no-new-privileges.

The opt-in `openwave.qa=browser-actions` suite uses a host-local HTTP fixture
through QEMU user networking. It exercises the actual Browser download manager,
Downloads page, installation menu, prompt renderer, file picker and appd. It
checks cancellation, Save As, automatic saving with a collision suffix, file
bytes and user ownership, installation cancellation, creation, duplicate
rejection and launch. The Shell verifies saved files using its existing
read-only access. The guest also rejects an unregistered install caller and
checks the installed app's live surface and confinement. The host checks that
the installed web app loads the fixture in a separate website-storage profile.
Normal boots do not enable either QA harness or the HTTP fixture.

Run host checks with:

```sh
python3 userspace/openwave-portal/tests/run_contract.py
python3 -m unittest discover -s userspace/security/selinux/tests -p 'test*.py'
python3 -m unittest scripts.tests.test_boot_image_pipeline scripts.tests.test_qemu_runner
```

Guest runs use `scripts/test-openwave-boot.py --browser-actions`, an explicit
image and kernel, and a fresh evidence directory. The runner uses an immutable
snapshot, verifies input hashes, captures serial evidence and requests orderly
ACPI/OpenRC shutdown. The fixture requires `CONFIG_VIRTIO_NET=y`; ordinary
`run-openwave` USB Wi-Fi operation does not use this test NIC.

Guest appd, portal and prompt logs in `/run/openwave` and Ocean logs in
`/run/user/1000/openwave-logs` are tmpfs-backed. The evidence directory's
`serial.log`, `http-fixture.json` and `result.json` preserve the test results
after shutdown. A passing source build alone does not prove guest success.

## Accepted Browser image

The enforcing Browser suite passed on:

```sh
cd /mnt/aosp/ThunderOS
GDK_BACKEND=x11 ./run-openwave run --window \
  --image out/boot-recovery/ocean-browser-actions-47.img \
  --kernel out/boot-recovery/bzImage-browser-38 \
  --usb-wifi 04e8:2018 --usb-bluetooth none --audio-backend none
```

Image SHA-256:
`8f5e4afe981d0c96c0ed9d951032f750f65bbb844e3dc89854df90a5caa1e1f2`.
Kernel SHA-256:
`3dfc36e04ba24862fb56112324af400fcd330eff4e1099b92eb264e7dc67ea81`.

`out/boot-recovery/browser-actions-47-acceptance/result.json` reports
`browser_actions_passed=true`, enforcing mode, unchanged input hashes,
complete boot, guest Power down and QEMU exit code zero. Serial evidence
records `AT_SECURE=1`, visible permission/save/install prompts, both completed
downloads and their bytes/ownership, and the installed web app's live surface
and confinement. Both website-storage reports in `http-fixture.json` contain
`stored=null`: Browser storage was not inherited by the installed web app.

The candidate preserves the user's latest image 37 profile, copied after that
VM closed. Journal recovery and orphan cleanup were performed on a verified
copy, followed by a clean read-only filesystem check. The original source
hash remained unchanged; provenance is recorded in
`out/boot-recovery/browser-latest-37-source.json`. Nine staged runtime files
were overlaid, and the empty staged `/run/dbus` directory was restored before
the full inode-label check. No image was mounted, and `OpenWave.img` and the
original image 37 were not modified. QA changes to preferences, downloads and
installed apps were discarded with the test snapshot.

Host validation passed the portal IPC contract, nine compiled-policy/context
tests and seventeen image-pipeline/QEMU-runner tests. The controlled HTTP test
uses VirtIO networking; physical USB Wi-Fi association was not repeated here.

The same image/kernel also passed
`browser-actions-47-portal-regression/result.json` and
`browser-actions-47-keyboard-regression/result.json` under `out/boot-recovery`.
The former covers Settings Wallpaper cancellation, Files and Photos selection,
descriptor delivery, crop, save and refresh. The latter injects real VirtIO
keyboard letters, Shift and Backspace into Ocean and Settings. Both retain
enforcing SELinux, unchanged input hashes, complete boot reports, guest Power
down and QEMU exit code zero.

## Changed sources and runtime staging

Core repairs are in `userspace/prompt-client/PromptTransport.cpp`,
`userspace/openwave-portal/main.c`,
`userspace/security/selinux/openwave.cil` and
`userspace/security/selinux/file_contexts`. The portal contract and policy
checks changed in `userspace/openwave-portal/tests/portal_contract.py`,
`userspace/security/selinux/tests/test_policy.py` and
`userspace/security/selinux/tests/test_file_contexts.py`.

`build-openwave` enables the VirtIO network driver. QA entry points are in
`OceanLauncher/OceanBrowser/main.cpp`,
`OceanLauncher/OceanBrowser/Main.qml` (the Downloads page object name), and
`OceanLauncher/OceanLauncher/main.cpp`. The opt-in drivers are
`userspace/openrc/qa/BrowserActionsHarness.h`,
`userspace/openrc/qa/BrowserActionsShellHarness.h` and
`userspace/openrc/qa/browser-actions.start`. Packaging/testing support changed
in `scripts/prepare-openwave-boot.py`, `scripts/test-openwave-boot.py` and
`scripts/openwave_browser_fixture.py`. The portal README and this report
document the changes.

The repaired prompt library is installed into the target SDK sysroot and
runtime rootfs. Browser, Shell and portal executables are installed into both
userspace staging and rootfs. Policy/configuration and the opt-in QA startup
script are staged in rootfs. Exact runtime file hashes are recorded in
`out/boot-recovery/ocean-browser-actions-47.runtime.json`; all nine published
files were verified against rootfs after guest tests. The deployable candidate
was repacked; the main `OpenWave.img` was left unchanged.
