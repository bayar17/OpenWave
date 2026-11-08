"""Configure-time regression checks for the recovered WaterRouge gallery.

No real Qt SDK is needed: fake CMake package exports let us verify that
missing library targets cause a visible skip or a strict failure, not an
incomplete executable.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "userspace/controls-gallery"


class GallerySDKGuardTests(unittest.TestCase):
    def setUp(self):
        if not shutil.which("cmake") or not shutil.which("c++"):
            self.skipTest("CMake and host C++ compiler required")
        self.tmp = tempfile.TemporaryDirectory(prefix="gallery-sdk-guard-")
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name)
        prefix = self.path / "prefix/lib/cmake"
        qt = prefix / "Qt6"
        sdk = prefix / "OpenWaveSDK"
        qt.mkdir(parents=True)
        sdk.mkdir(parents=True)
        (qt / "Qt6Config.cmake").write_text("""
foreach(component IN LISTS Qt6_FIND_COMPONENTS)
  if(NOT TARGET Qt6::${component})
    add_library(Qt6::${component} INTERFACE IMPORTED)
  endif()
  set(Qt6_${component}_FOUND TRUE)
endforeach()
macro(qt_standard_project_setup)
endmacro()
function(qt_add_executable target)
  add_executable(${target} ${ARGN})
endfunction()
function(qt_add_qml_module target)
endfunction()
set(Qt6_FOUND TRUE)
""")
        (qt / "Qt6ConfigVersion.cmake").write_text("""
set(PACKAGE_VERSION "6.10.3")
if(PACKAGE_FIND_VERSION VERSION_LESS_EQUAL PACKAGE_VERSION)
  set(PACKAGE_VERSION_COMPATIBLE TRUE)
endif()
""")
        self.sdk = sdk / "OpenWaveSDKConfig.cmake"
        self.prefix = self.path / "prefix"

    def configure(self, target_set, strict=False):
        entries = [
            "set(OpenWaveSDK_FOUND TRUE)",
            "set(OpenWaveSDK_Fonts_FOUND TRUE)",
        ]
        for target in target_set:
            entries.append(f"add_library(OpenWave::{target} INTERFACE IMPORTED)")
        self.sdk.write_text("\n".join(entries) + "\n")
        build = self.path / ("full" if len(target_set) == 4 else
                             "strict" if strict else "missing")
        run = subprocess.run([
            "cmake", "-S", str(SOURCE), "-B", str(build),
            f"-DCMAKE_PREFIX_PATH={self.prefix}",
            f"-DOPENWAVE_REQUIRE_CONTROLS_GALLERY={'ON' if strict else 'OFF'}",
        ], capture_output=True, text=True, timeout=60)
        return run, run.stdout + run.stderr

    def test_missing_targets_skips_only_gallery(self):
        process, log = self.configure(("Fonts",))
        self.assertEqual(process.returncode, 0, log)
        self.assertIn("Skipping WaterRouge Controls Gallery", log)
        for name in ("OpenWave::Application", "OpenWave::Controls", "OpenWave::Theme"):
            self.assertIn(name, log)

    def test_strict_mode_catches_missing_targets(self):
        process, log = self.configure(("Fonts",), strict=True)
        self.assertNotEqual(process.returncode, 0, log)
        self.assertIn("requires missing SDK targets", log)

    def test_complete_sdk_configures_gallery_target(self):
        process, log = self.configure(("Application", "Controls", "Fonts", "Theme"))
        self.assertEqual(process.returncode, 0, log)
        self.assertNotIn("Skipping WaterRouge Controls Gallery", log)
        self.assertTrue((self.path / "full/CMakeFiles/water-rouge-gallery.dir").is_dir())


if __name__ == "__main__":
    unittest.main()
