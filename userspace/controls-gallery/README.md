# Controls Gallery

This preserves the deployed OpenWave Controls Gallery under its existing app ID.
`Main.qml` was recovered byte-for-byte from the raw QML resource embedded in the
previous `water-rouge-gallery` executable (build ID
`ba14eaaddd74b09beb82cd8deb80bb57b5f7233e`). The original custom source was absent
from this checkout; the upstream Qt Gallery example is a different application.
The previous executable required Qt6.11. This build uses the current exported
OpenWave SDK and Qt6.10.3, retaining the original QML URI and existing manifest.
The C++ entrypoint initializes the same OpenWave Application framework.
The recovered QML now explicitly imports Qt Quick Controls, which provides its
`ButtonGroup`, `GroupBox`, `Label`, and `ScrollView` types.

Recovery evidence and the read-only ELF resource extraction script are under
`out/boot-recovery/gallery-recovered` and
`out/boot-recovery/recover-gallery-resources.py` for this session.
