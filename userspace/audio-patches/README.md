# OpenWave audio patches

The PipeWire 1.4.9 patches are applied idempotently by `build-openwave audio`
and `build-openwave bluetooth-audio`.

`pipewire-1.4.9-openwave-app-access.patch` bounds access to the system audio
server using its immutable Unix peer UID. Root system services keep their
existing permissions. Isolated non-root applications can create playback
client nodes, access their own nodes, and read output-device information.
Server-side plugin factories and capture devices remain inaccessible. Physical
nodes cannot be modified, and session-manager updates cannot widen these limits.
Link creation checks both stream owners, including when a trusted manager
creates the link. Applications cannot forge `pipewire.sec.uid`.

appd publishes the playback socket only with the matching patched library.
Rebuild the audio stack and restart OpenWave when updating an older SDK/runtime.
`build-openwave test-permissions` validates playback and denial using virtual
devices in a separate QEMU test image.

`/etc/alsa/conf.d/99-openwave-pipewire.conf` activates the installed PipeWire
ALSA configuration. ALSA playback connects to appd's existing playback socket;
isolated applications do not need access to `/dev/snd`. Capture remains denied
by the existing server policy. The configuration reuses PipeWire's installed
ALSA definitions from commit `fd60e04525f3a04d90bf50085222e0cc9139b4a4`.
