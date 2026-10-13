# OpenWave application clients

Link `OpenWave::Applications`. `<OpenWave/ApplicationsClient.h>` provides async
registry/lifecycle queries and signals from appd. The compatible
`OpenWaveApplications::launch(appId, arguments, owner, completion)` in
`<OpenWave/ApplicationChannel.h>` now calls appd on the system bus. Completion
indicates handoff acceptance; lifecycle signals and Ocean report later errors.

Ocean crossfades into the loading screen, then commits appd's launch ticket.
The daemon creates or activates the process; Ocean associates its Wayland PID
and crossfades into the application. The shell owns no application QProcess.

Applications keep `openwave-app-<PID>.socket` for activation and graceful quit,
using bounded JSON, timeouts and peer credentials. Files accepts a confined
absolute path; Files and Browser support graceful Qt shutdown. appd verifies
peer UID/PID, escalates after its deadline and cleans groups/cgroups and helpers.

See [appd's architecture and IPC documentation](../appd/README.md) for metadata,
boot, restart policy and root-session limitations. Configure `tests/` with the
exported target SDK. Channel tests check instance transport; layout tests need
appd. The appd QEMU harness runs both alongside lifecycle integration tests.
