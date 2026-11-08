cmake_minimum_required(VERSION 3.21)
project(OpenWaveControlsGallery LANGUAGES CXX)
find_package(Qt6 6.10 REQUIRED COMPONENTS Quick QuickControls2)

# The recovered WaterRouge gallery still uses the older SDK application,
# controls and theme modules. The current exported SDK provides Fonts but does
# not yet install the other three. Never fabricate missing imported targets or
# mark missing SDK components FOUND: this would create a broken executable.
option(OPENWAVE_REQUIRE_CONTROLS_GALLERY
    "Require complete WaterRouge SDK APIs instead of omitting the recovered gallery"
    OFF)
find_package(OpenWaveSDK REQUIRED COMPONENTS Fonts)
set(_openwave_gallery_missing "")
foreach(_component IN ITEMS Application Controls Fonts Theme)
    if(NOT TARGET OpenWave::${_component})
        list(APPEND _openwave_gallery_missing "OpenWave::${_component}")
    endif()
endforeach()
if(_openwave_gallery_missing)
    list(JOIN _openwave_gallery_missing ", " _openwave_gallery_missing_message)
    set(_openwave_gallery_diagnostic
        "The recovered WaterRouge Controls Gallery requires missing SDK targets: "
        "${_openwave_gallery_missing_message}. Restore the Application, Controls "
        "and Theme SDK exports before shipping this gallery.")
    if(OPENWAVE_REQUIRE_CONTROLS_GALLERY)
        message(FATAL_ERROR "${_openwave_gallery_diagnostic}")
    endif()
    message(WARNING
        "Skipping WaterRouge Controls Gallery (not the rest of userspace). "
        "${_openwave_gallery_diagnostic} "
        "Set -DOPENWAVE_REQUIRE_CONTROLS_GALLERY=ON to make this a hard error.")
    return()
endif()
unset(_component)
unset(_openwave_gallery_missing)
unset(_openwave_gallery_missing_message)
unset(_openwave_gallery_diagnostic)

qt_standard_project_setup(REQUIRES 6.10)
qt_add_executable(water-rouge-gallery main.cpp)
qt_add_qml_module(water-rouge-gallery
    URI OpenWave.ControlsGallery VERSION 1.0
    IMPORT_PATH "${OpenWaveSDK_QML_IMPORT_PATH}"
    QML_FILES Main.qml)
target_link_libraries(water-rouge-gallery PRIVATE Qt6::Quick Qt6::QuickControls2
    OpenWave::Application OpenWave::Controls OpenWave::Fonts OpenWave::Theme)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/OpenWaveBundle.cmake")
openwave_install_bundle(water-rouge-gallery com.openwave.waterrouge-gallery SYSTEM)
