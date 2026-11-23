function(openwave_add_web)
    if(TARGET openwave-web)
        return()
    endif()
    set(web "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    find_package(Qt6 6.10 REQUIRED COMPONENTS Quick Network)
    qt_policy(SET QTP0001 NEW)
    set(include_root "${CMAKE_CURRENT_BINARY_DIR}/openwave-web-include")
    foreach(header WebRuntime.h WebInput.h WebsiteIconFetcher.h)
        configure_file("${web}/${header}" "${include_root}/OpenWave/${header}" COPYONLY)
    endforeach()
    qt_add_library(openwave-web STATIC)
    add_library(OpenWave::Web ALIAS openwave-web)
    set_target_properties(openwave-web PROPERTIES POSITION_INDEPENDENT_CODE ON AUTOMOC ON)
    target_compile_features(openwave-web PUBLIC cxx_std_17)
    target_sources(openwave-web PRIVATE
        "${web}/WebRuntime.cpp" "${web}/WebRuntime.h" "${web}/WebInput.cpp" "${web}/WebInput.h"
        "${web}/WebsiteIconFetcher.cpp" "${web}/WebsiteIconFetcher.h")
    target_include_directories(openwave-web PUBLIC "${include_root}" "${include_root}/OpenWave" "${web}"
        "${CMAKE_SYSROOT}/usr/include/glib-2.0" "${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include"
        "${CMAKE_SYSROOT}/usr/include/gio-unix-2.0"
        "${CMAKE_SYSROOT}/usr/include/wpe-webkit-2.0"
        "${CMAKE_SYSROOT}/usr/include/wpe-webkit-2.0/wpe-platform"
        "${CMAKE_SYSROOT}/usr/include/libsoup-3.0")
    target_link_libraries(openwave-web PUBLIC Qt6::Quick Qt6::Network gio-2.0 gobject-2.0 glib-2.0 WPEWebKit-2.0)
    set_source_files_properties("${web}/WebView.qml" PROPERTIES QT_RESOURCE_ALIAS WebView.qml)
    qt_add_qml_module(openwave-web
        URI OpenWave.Web VERSION 1.0
        OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml/OpenWave/Web"
        IMPORT_PATH "${CMAKE_SYSROOT}/usr/lib64/qt6/qml"
        QML_FILES "${web}/WebView.qml")
    set(import_source "${CMAKE_CURRENT_BINARY_DIR}/openwave-web-import.cpp")
    file(GENERATE OUTPUT "${import_source}" CONTENT "#include <QtQml/qqmlextensionplugin.h>\nQ_IMPORT_QML_PLUGIN(OpenWave_WebPlugin)\n")
    set(OPENWAVE_WEB_IMPORT_SOURCE "${import_source}" CACHE INTERNAL "")
endfunction()

function(openwave_register_web target)
    target_link_libraries(${target} PRIVATE openwave-web openwave-webplugin)
    target_sources(${target} PRIVATE "${OPENWAVE_WEB_IMPORT_SOURCE}")
endfunction()
