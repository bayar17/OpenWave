# System power actions

Settings > General > Power options offers Restart and Shut down with a
confirmation prompt. These controls are no longer in Ocean's Quick Menu.
`powerd` owns `org.openwave.Power` on the system bus and authenticates the
supervised Ocean UID, PID and process start time against OpenRC's protected
identity record. It also accepts the live managed Settings leader, authenticated
through appd's protected identity registry and administrator flag. Ordinary apps,
Settings child processes, arbitrary session-user processes and root command-line
callers cannot request a power action. Settings checks the supervised service
asynchronously, updates button availability when it disappears or returns, and
does not signal PID 1 or require root privileges.

The broker writes only `poweroff` or `reboot` to the verified, root-owned OpenRC
FIFO `/run/openrc/init.ctl`. PID 1 performs the normal shutdown/reboot runlevel,
service teardown, sync and unmount. The broker has no reboot capability and
does not execute shell commands. Its FIFO has a dedicated SELinux type that
Ocean and applications cannot open. A missing FIFO returns a retryable error.

Logs are `/run/openwave/powerd.stdout.log` and `powerd.stderr.log` (tmpfs).

Run `python3 userspace/powerd/tests/test_fifo.py` to exercise the real FIFO
validation with temporary files and no host power action. Guest acceptance
uses `scripts/test-openwave-boot.py --power reboot` with an explicitly selected
image/kernel; it clicks the actual Settings controls, verifies cancellation,
a second boot ID, and actual poweroff.
See [the recovery report](../../docs/SETTINGS-POWER-UNINSTALL-RECOVERY.md) for artifacts
and the QEMU command.
