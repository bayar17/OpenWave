"""Host regression for the isolated production appd rebuild."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class AppdOnlyBuildTests(unittest.TestCase):
    def test_only_real_sdk_components_are_required(self):
        cmake = (ROOT / "userspace/appd-only/CMakeLists.txt").read_text()
        self.assertIn('find_package(OpenWaveSDK REQUIRED COMPONENTS Xml Identity Applications)', cmake)
        self.assertIn('add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../appd"', cmake)
        self.assertIn("add_library(openwave-selinux STATIC", cmake)
        for missing in ("OpenWave::Application ", "OpenWave::Controls", "OpenWave::Theme"):
            self.assertNotIn(missing, cmake)
        build = (ROOT / "build-openwave").read_text()
        self.assertIn("build_appd_only() (", build)
        self.assertIn('DESTDIR="$OPENWAVE_RUNTIME_STAGE" cmake --install', build)
        self.assertIn("Stage 2H control marker is absent", build)
        self.assertIn("userspace|appd-only|all)", build)
        self.assertEqual(subprocess.run(["bash", "-n", str(ROOT / "build-openwave")]).returncode, 0)

    def test_cmake_configures_without_waterrouge_sdk_targets(self):
        with tempfile.TemporaryDirectory(prefix="openwave-appd-only-cmake-") as temp:
            root = Path(temp)
            packages = root / "prefix/lib/cmake"
            qt = packages / "Qt6"
            sdk = packages / "OpenWaveSDK"
            qt.mkdir(parents=True)
            sdk.mkdir(parents=True)
            (qt / "Qt6Config.cmake").write_text("""
foreach(name IN LISTS Qt6_FIND_COMPONENTS)
  if(NOT TARGET Qt6::${name})
    add_library(Qt6::${name} INTERFACE IMPORTED)
  endif()
  set(Qt6_${name}_FOUND TRUE)
endforeach()
set(Qt6_FOUND TRUE)
""")
            (qt / "Qt6ConfigVersion.cmake").write_text("""
set(PACKAGE_VERSION "6.10.3")
if(PACKAGE_FIND_VERSION VERSION_LESS_EQUAL PACKAGE_VERSION)
  set(PACKAGE_VERSION_COMPATIBLE TRUE)
endif()
""")
            (sdk / "OpenWaveSDKConfig.cmake").write_text("""
foreach(name IN LISTS OpenWaveSDK_FIND_COMPONENTS)
  if(NOT TARGET OpenWave::${name})
    add_library(OpenWave::${name} INTERFACE IMPORTED)
  endif()
  set(OpenWaveSDK_${name}_FOUND TRUE)
endforeach()
set(OpenWaveSDK_FOUND TRUE)
""")
            fake_selinux = root / "fake/libselinux.so"
            fake_selinux.parent.mkdir()
            fake_selinux.write_bytes(b"")
            result = subprocess.run(
                ["cmake", "-S", str(ROOT / "userspace/appd-only"),
                 "-B", str(root / "build"),
                 "-DCMAKE_PREFIX_PATH=" + str(root / "prefix"),
                 "-DOPENWAVE_SELINUX_LIBRARY=" + str(fake_selinux),
                 "-DCMAKE_INSTALL_PREFIX=/usr"],
                capture_output=True, text=True, timeout=45,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue((root / "build/appd/CMakeFiles/appd.dir").is_dir())
            self.assertTrue((root / "build/appd/CMakeFiles/openwave-launch.dir").is_dir())


if __name__ == "__main__":
    unittest.main()
