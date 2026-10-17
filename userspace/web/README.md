# OpenWave Web

`OpenWave.Web 1.0` provides an embeddable Qt Quick `WebView` backed by OpenWave's
installed WPE WebKit engine. This SDK module is separate from the XDG desktop
Settings portal; it adds no rendering daemon or XDG interface.

Build with `./build-openwave sdk` after installing browser dependencies.
`OPENWAVE_BUILD_WEB` defaults on when the target WPE Qt plugin exists. Explicitly
enabling it without WPE fails configuration. The runtime assembler includes the
shared library and QML module.

```cmake
find_package(Qt6 6.10 REQUIRED COMPONENTS Quick)
find_package(OpenWaveSDK REQUIRED COMPONENTS Web)
qt_standard_project_setup(REQUIRES 6.10)
qt_add_executable(myapp main.cpp)
qt_add_qml_module(myapp URI MyApp VERSION 1.0
    IMPORT_PATH ${OpenWaveSDK_QML_IMPORT_PATH}
    QML_FILES Main.qml)
target_link_libraries(myapp PRIVATE Qt6::Quick OpenWave::Web)
```

Select OpenGL before creating Qt Quick windows. Initialize on the GUI thread
after constructing the application and before loading QML:

```cpp
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <OpenWave/WebRuntime.h>

int main(int argc, char **argv)
{
    OpenWave::WebRuntime::configureGraphics();
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    if (!OpenWave::WebRuntime::initialize(&engine)) return 1;
    engine.loadFromModule("MyApp", "Main");
    if (engine.rootObjects().isEmpty()) return 1;
    return app.exec();
}
```

```qml
import QtQuick
import QtQuick.Window
import OpenWave.Web 1.0

Window {
    width: 544; height: 960; visible: true
    WebView {
        anchors.fill: parent
        url: "https://example.org"
        onLoadFinished: function(success, address, error) {
            if (!success && error.length) console.warn(error)
        }
    }
}
```

The component preserves WPE's `url`, `title`, `zoomLevel`, `loadProgress`,
`canGoBack`, `canGoForward`, `userScripts`, `privateBrowsing`, and methods
`goBack`, `goForward`, `reload`, `stop`, `loadHtml(html, baseUrl)`, and
`runJavaScript(script, callback)`. Wait for `ready` before invoking JavaScript or
`loadHtml`. WPE does not load `qrc:` pages; use `loadHtml` for bundled HTML.
Set `userScripts` and `privateBrowsing` at construction time.

Read-only `ready` indicates native view creation and input attachment.
`navigationLoading` tracks actual navigation signals, and `errorText` contains
the last load failure. `loadFinished(success, address, error)` reports terminal
success, failure, or cancellation (false with an empty error). Redirects do not
emit intermediate completions. Loading state does not depend on progress
reaching 100. The inherited WPE signals remain available to apps.

Private browsing is the default. Views within one process share WPE's ephemeral
private session, discarded when its last view closes. This module does not set
up persistent profiles or share Ocean's cookies across processes. Ocean chooses
its existing regular/private sessions and keeps its own history, cookies,
downloads, and file chooser integration. The SDK does not grant website
permissions, approve TLS exceptions, or supply file picker/download UI. Apps
continue using native resource brokers and their existing sandbox. Embedding
a view is not a separate security boundary.

The module shares Ocean's pointer adapter, including mouse/VNC drag scrolling,
real touch, wheel input, and gesture cancellation when hidden. `OpenWave::WebInput`
is also available for existing raw WPE views. Initialization is idempotent and
supplies a single bounded GLib event pump per application for the current target
Qt build without a GLib dispatcher. It uses the installed engine without a WPE
source rebuild.

`examples/` builds a standalone app from the exported SDK. `tests/` exercises real
navigation, JavaScript, redirects, cancellation and failure. Ocean's
`browser-input-tests` uses this module to check touch and drag input. Rendering
tests need OpenWave's EGL/Wayland runtime, rather than the software/offscreen
Qt renderer.

The engine wrapper draws on KDE/Angelfish commit
`2a82873b85f8b608c3b4c8cbe6244e9bc1396027`, `lib/contents/ui/WebView.qml`, for
its design approach only. Angelfish uses Qt WebEngine; its engine code
was not copied. WPE integration was checked against OpenWave's vendor snapshot,
`Source/WebKit/UIProcess/API/wpe/qt6/WPEQtView.h` (SHA-256
`9608e4600e6c803aa0ebdbb464914d1f4905c5f4c4fec1959791c1a7d590dd11`) and
`WPEQtView.cpp`
(`a6e2306d0ce50a53b0c6b930e6ac61f9a03fdd155db1f3a74942e541d6c17c07`).
The vendor tree has no Git metadata identifying its upstream commit.

`<OpenWave/WebsiteIconFetcher.h>` offers asynchronous website icon selection for
native launchers. Pass the current page URL and the JSON returned by
`WebsiteIconFetcher::discoveryScript()` using the WebView's `runJavaScript`.
`finished(png)` returns the highest-resolution usable website icon normalized to
at most 512 pixels, or empty bytes for the default-icon fallback. `cancel()`
discards pending requests. It does not install apps or grant bundle write access.
