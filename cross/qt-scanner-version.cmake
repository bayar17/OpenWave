include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${OPENWAVE_PACKAGE_DIR}/Qt6WaylandScannerToolsConfigVersion.cmake"
    VERSION "${OPENWAVE_QT_VERSION}" COMPATIBILITY ExactVersion)
