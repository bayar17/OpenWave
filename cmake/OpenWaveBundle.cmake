set(_OPENWAVE_BUNDLE_DIR "${CMAKE_CURRENT_LIST_DIR}")
find_program(OPENWAVE_BUNDLE_PYTHON NAMES python3 python REQUIRED)

set(OPENWAVE_BUNDLE_DIRECTORIES bin sbin lib lib64 plugins qml
    resources resources/images resources/sounds resources/fonts
    share share/icons share/translations share/data)

function(openwave_install_bundle target id)
    cmake_parse_arguments(BUNDLE "SYSTEM" "" "" ${ARGN})
    set(bundle "/Applications/${id}")
    set(generator_options "")
    if(BUNDLE_SYSTEM)
        set(bundle "/System/Applications/${id}")
        set(generator_options --system)
    endif()
    set(declaration "${CMAKE_CURRENT_SOURCE_DIR}/Application.toml")
    if(NOT EXISTS "${declaration}")
        message(FATAL_ERROR "${id}: Application.toml is missing next to ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")
    endif()
    set(manifest "${CMAKE_CURRENT_BINARY_DIR}/${id}.manifest.json")
    set(desktop "${CMAKE_CURRENT_BINARY_DIR}/${id}.desktop")
    add_custom_command(OUTPUT "${manifest}" BYPRODUCTS "${desktop}"
        COMMAND "${OPENWAVE_BUNDLE_PYTHON}" "${_OPENWAVE_BUNDLE_DIR}/openwave-bundle-manifest.py"
                "${declaration}" "${manifest}" ${generator_options} --desktop "${desktop}"
        DEPENDS "${declaration}" "${_OPENWAVE_BUNDLE_DIR}/openwave-bundle-manifest.py"
        COMMENT "Generating the manifest of ${id} from Application.toml" VERBATIM)
    add_custom_target(${target}_manifest ALL DEPENDS "${manifest}")
    add_dependencies(${target} ${target}_manifest)
    set_target_properties(${target} PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "${bundle}/lib;${bundle}/lib64;/usr/lib64;/usr/lib")
    install(TARGETS ${target} RUNTIME DESTINATION "${bundle}/bin")
    install(FILES "${declaration}" DESTINATION "${bundle}")
    install(FILES "${manifest}" DESTINATION "${bundle}" RENAME manifest.json)
    install(FILES "${desktop}" DESTINATION "${bundle}/share/applications" OPTIONAL)
    foreach(directory ${OPENWAVE_BUNDLE_DIRECTORIES})
        install(DIRECTORY DESTINATION "${bundle}/${directory}")
    endforeach()
endfunction()
