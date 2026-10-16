# OpenWave fonts

Source Sans Pro is installed once in `/usr/share/fonts/openwave/SourceSansPro`.
Its SIL Open Font License is installed in `/usr/share/licenses/SourceSansPro`.
The unmodified files come from Adobe's [Source Sans Pro 2.045 Roman / 1.095
Italic release](https://github.com/adobe-fonts/source-sans/releases/tag/2.045R-ro/1.095R-it).
The upstream ZIP SHA-256 is
`5f090b821e117f452ef399c3c1b8b244c1647728a1e901c4d167cbf7fc298ab6`.
All six weights and their italic faces are included.

`OpenWave::Fonts` provides a shared native library and the typed Qt Quick module
`OpenWave.Fonts`. Font files are not embedded in each app. QML type metadata is
installed in the SDK for tooling; only the runtime plugin and qmldir reach rootfs.

## Initialization

```cpp
#include <QGuiApplication>
#include <OpenWave/FontManager.h>

QGuiApplication app(argc, argv);
OpenWave::FontManager::initialize();
```

Initialize on the GUI thread before loading QML. This registers the central files
with Qt, including its embedded FreeType backend, and sets the application's
default family to Source Sans Pro. Repeated initialization preserves later app
font overrides. Missing fonts leave Qt's fallback in place and log a diagnostic.
Existing explicit families, sizes, and weights still use Qt's normal properties.

## Qt Quick

```qml
import QtQuick
import OpenWave.Fonts 1.0

Text {
    text: qsTr("OpenWave")
    font: Fonts.font(28, Font.Light)
}
```

Standard font properties also work:

```qml
font.family: Fonts.family
font.pointSize: 14
font.weight: Font.Normal
```

| API | Meaning |
| --- | --- |
| `Fonts.systemFamily` | The system typeface name, `Source Sans Pro` |
| `Fonts.family` | The current application's default family |
| `Fonts.defaultFont` | Its default `QFont` |
| `Fonts.sourceSansProAvailable` | Whether Qt can resolve Source Sans Pro |
| `Fonts.families` | Locale-sorted fonts available to this process |
| `Fonts.styles(family)` | Available face styles |
| `Fonts.isAvailable(family)` | Case-insensitive family lookup |
| `Fonts.font(pixelSize, weight, italic, family)` | A standard `QFont`; the last three arguments are optional |

The factory defaults to normal weight, upright style, and the app's family.
Unknown families fall back to that family; sizes and weights are bounded.
Qt handles glyph fallback. Use `font.pointSize` for point sizing or the existing
app scale with pixel sizes; this API does not impose another scaling system.

The catalog updates when Qt's font database changes, and default font bindings
update when the application font changes. This provides a catalog for a future
Fontbook. System font installation, persistent selection, and Fontbook UI are
not implemented by this component.

## CMake

```cmake
find_package(Qt6 6.10 REQUIRED COMPONENTS Quick)
find_package(OpenWaveSDK REQUIRED COMPONENTS Fonts)
qt_standard_project_setup(REQUIRES 6.10)
qt_add_executable(myapp main.cpp)
qt_add_qml_module(myapp
    URI MyApp VERSION 1.0
    IMPORT_PATH "${OpenWaveSDK_QML_IMPORT_PATH}"
    QML_FILES Main.qml)
target_link_libraries(myapp PRIVATE Qt6::Quick OpenWave::Fonts)
```

Native tests outside OpenWave can call `initialize(directory)` with the SDK's
font installation or the source assets directory. Runtime apps normally use
`initialize()` without an override.
