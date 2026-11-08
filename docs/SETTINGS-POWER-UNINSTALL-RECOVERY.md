# Settings power and web-app uninstall recovery

Accepted x86_64 candidate: `out/boot-recovery/ocean-settings-power-49.img`.
The image was repacked with rebuilt target executables and policy. Both guest
acceptance suites passed with SELinux enforcing on 2026-11-06.

```sh
cd /mnt/aosp/ThunderOS
GDK_BACKEND=x11 ./run-openwave run --window \
  --image out/boot-recovery/ocean-settings-power-49.img \
  --kernel out/boot-recovery/bzImage-browser-38 \
  --usb-wifi 04e8:2018 --usb-bluetooth none --audio-backend none
```

Image SHA-256:
`b5ba89d30c7bfa306639b2ce1e60869e79abf002d23798bd173521ef823714f3`.
Kernel SHA-256:
`3dfc36e04ba24862fb56112324af400fcd330eff4e1099b92eb264e7dc67ea81`.

## Result and causes

Quick Menu no longer contains Restart or Shut down. Existing reveal, dimming,
reordering and Reduce Motion behavior are preserved. Power controls are in
Settings > General > Power Options, with confirmation and asynchronous service
availability. Settings now calls the supervised power broker instead of relying
on root privileges or signaling PID 1. The broker authenticates the live managed
Settings leader through appd, then submits a fixed command to OpenRC's protected
FIFO. Ordinary applications, Settings children and unregistered callers cannot
use this authority. Settings receives no FIFO access or kernel power capability.

Uninstall was stopping after quarantining the installed bundle because appd
lacked SELinux directory rename permission for the app's private profile.
The policy now permits that existing storage transaction. It still limits
appd to its existing private application-data type. The user's Instagram
bundle was already marked `.uninstall-...` in the source image; appd resumes
that pending removal when the repaired image starts. Other web apps can be
removed through Settings > Apps and the uninstall confirmation.

## Changed sources

- `OceanLauncher/OceanLauncher/QuickMenu.qml` and `Main.qml`: remove Quick Menu
  power controls and their signal handler.
- `OceanLauncher/OceanSettings/SettingsBackend.{cpp,h}`, `CMakeLists.txt` and
  `GeneralSettingsPage.qml`: supervised broker identity, asynchronous power
  requests, availability and busy feedback, with the target Identity SDK library.
- `userspace/powerd/main.c` and `README.md`: authenticate the managed Settings
  leader and document its authority.
- `userspace/appd/dbus.c`: allow the supervised power broker to resolve managed
  caller identity.
- `userspace/security/selinux/openwave.cil` and `tests/test_policy.py`: private
  profile rename, Settings service-identity metadata access, and assertions that
  Settings cannot access the FIFO or signal PID 1.
- `userspace/openrc/qa/{PowerShellHarness,SettingsPowerHarness,SettingsUninstallHarness,BrowserActionsShellHarness}.h`,
  `browser-actions.start`, and both applications' `main.cpp`: opt-in guest tests
  that exercise actual Settings controls and visible confirmation prompts.

These changes build on the existing dirty checkout; unrelated modifications
were retained. QA automation is disabled during normal boots.

## Validation and evidence

Target Settings, Ocean, appd and powerd builds passed. The eight target-runtime
uninstall-storage tests passed, including recovery, symlink rejection and
preservation of other apps' data. The power FIFO tests and all ten compiled
SELinux policy/context tests passed. Staged policy verification passed.

`out/boot-recovery/settings-power-49-uninstall/result.json` records
`browser_actions_passed=true`. The guest verifies Cancel preserves the app,
Confirm removes it from Settings, and bundle/profile cleanup succeeds. The
existing Browser download, web-app installation and confinement checks also
pass. Serial evidence includes the `UNINSTALL_QA_PASS` markers.

`out/boot-recovery/settings-power-49-reboot/result.json` records
`power_passed=true`. The test checks that Quick Menu power buttons are absent,
clicks Settings Restart, cancels and then confirms, observes an actual reboot
and a new boot ID, and repeats the Settings flow for shutdown. The guest reports
`Restarting system` and `Power down`, and QEMU exits successfully. Unregistered
power callers are rejected.

Both runs use snapshots, report enforcing mode, complete boot, no kernel panic,
unchanged image/kernel hashes and QEMU exit code zero. Test downloads, preferences
and app installations are discarded. Normal service logs under `/run/openwave`
and app logs under `/run/user/1000/openwave-logs` are tmpfs-backed; the test
directories retain `serial.log` and `result.json` after shutdown. Physical USB
Wi-Fi was not retested in this change.

## Data preservation and packing

The source was the user's latest closed image 47, SHA-256
`08863736a6efe950778d86ef56403a2098e4f3650c7b34eafaadadb3cd63b160`.
`out/boot-recovery/settings-power-48-source.json` records its provenance.
Journal recovery and orphan cleanup ran on a verified copy, followed by a clean
read-only filesystem check. Existing Browser data and saved network settings
were retained. Original image 47 and `OpenWave.img` were not modified.

Rebuilt runtime files were installed into both `rootfs` and
`out/staging/x86_64-unknown-openwave/userspace` before overlaying nine allowlisted
runtime files into the unpublished candidate with debugfs. The replaced inodes
were labeled from the staged SELinux contexts and verified by readback. No image
was mounted. `out/boot-recovery/ocean-settings-power-49.runtime.json` records
each runtime file's SHA-256; all nine match both staging trees and image readback.
