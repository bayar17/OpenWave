# OpenWave XML library

`libopenwave-xml.so.0` uses the existing target libxml2 library to parse local
XML documents. It returns errors through `GError` and does not load external
DTDs or expand external entities. Documents are limited to 1 MiB.

The SDK exports `<OpenWave/OpenWaveXml.h>` and the CMake target `OpenWave::Xml`.
The header supports C and C++ callers. Include the `Xml` component when finding
`OpenWaveSDK`, then link `OpenWave::Xml`.

| Function | Result |
| --- | --- |
| `openwave_xml_load_file(filename, error)` | Parsed document serialized as UTF-8; release with `g_free` |
| `openwave_xml_load_dbus_file(filename, interface, error)` | D-Bus node describing exactly the requested interface; release with `g_dbus_node_info_unref` |
| `openwave_xml_load_dbus_interface(interface, error)` | Same, loaded from `/usr/share/dbus-1/interfaces/<interface>.xml` |

Canonical daemon contracts live in `userspace/dbus/interfaces/`. Both the SDK
and runtime build install these files. Libxml2 parses and validates the XML;
GIO then converts D-Bus declarations into `GDBusNodeInfo`. Missing or invalid
contracts stop daemon startup with an error instead of falling back to an
embedded definition. Qt clients retain their existing D-Bus calls.

The tests in `tests/` build against the exported SDK and cover every daemon
contract, UTF-8, malformed XML, DTD rejection, missing files, interface identity
and the document size limit. Target tests need the OpenWave loader and libraries,
or an OpenWave environment, to run.
