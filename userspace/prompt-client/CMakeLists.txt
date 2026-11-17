find_package(Qt6 REQUIRED COMPONENTS Core)
add_library(openwave-prompt-client STATIC
    PromptTransport.cpp PromptTransport.h
    OpenWavePromptClient.cpp OpenWavePromptClient.h
    PromptRendererBackend.cpp PromptRendererBackend.h)
set_target_properties(openwave-prompt-client PROPERTIES AUTOMOC ON)
target_compile_features(openwave-prompt-client PUBLIC cxx_std_17)
target_link_libraries(openwave-prompt-client PUBLIC Qt6::Core)
target_include_directories(openwave-prompt-client PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
if(CMAKE_CROSSCOMPILING)
    target_include_directories(openwave-prompt-client PRIVATE
        "${CMAKE_SYSROOT}/usr/include/glib-2.0"
        "${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include")
    target_link_libraries(openwave-prompt-client PRIVATE gio-2.0 gobject-2.0 glib-2.0)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PROMPT_GIO REQUIRED IMPORTED_TARGET gio-2.0)
    target_link_libraries(openwave-prompt-client PRIVATE PkgConfig::PROMPT_GIO)
endif()
