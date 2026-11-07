#!/usr/bin/env python3
"""Verify effective OpenWave file contexts using the actual libselinux matcher."""
import ctypes
import ctypes.util
from pathlib import Path
import stat
import unittest


FILE_CONTEXTS = Path(__file__).resolve().parents[1] / "file_contexts"


class Option(ctypes.Structure):
    _fields_ = [("type", ctypes.c_int), ("value", ctypes.c_char_p)]


class FileContexts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = ctypes.util.find_library("selinux")
        if not path:
            raise unittest.SkipTest("Host libselinux is missing")
        cls.lib = ctypes.CDLL(path)
        cls.lib.selabel_open.argtypes = (ctypes.c_uint, ctypes.POINTER(Option), ctypes.c_uint)
        cls.lib.selabel_open.restype = ctypes.c_void_p
        cls.lib.selabel_lookup_raw.argtypes = (
            ctypes.c_void_p, ctypes.POINTER(ctypes.c_char_p), ctypes.c_char_p, ctypes.c_int
        )
        cls.lib.selabel_lookup_raw.restype = ctypes.c_int
        cls.lib.selabel_close.argtypes = (ctypes.c_void_p,)
        cls.lib.freecon.argtypes = (ctypes.c_void_p,)
        # SELABEL_CTX_FILE=0, SELABEL_OPT_PATH=3.
        opts = (Option * 1)(Option(3, str(FILE_CONTEXTS).encode()))
        cls.handle = cls.lib.selabel_open(0, opts, 1)
        if not cls.handle:
            raise RuntimeError("Cannot load file-context spec: " + str(FILE_CONTEXTS))

    @classmethod
    def tearDownClass(cls):
        if getattr(cls, "handle", None):
            cls.lib.selabel_close(cls.handle)

    def assert_type(self, path, expected):
        result = ctypes.c_char_p()
        rc = self.lib.selabel_lookup_raw(
            self.handle, ctypes.byref(result), path.encode(), stat.S_IFREG
        )
        self.assertEqual(rc, 0, "No matching context for " + path)
        self.assertIsNotNone(result.value)
        try:
            self.assertIn(":" + expected + ":", result.value.decode(), path)
        finally:
            self.lib.freecon(result)

    def test_dynamic_loaders(self):
        for path in (
            "/lib64/ld-linux-x86-64.so.2",
            "/usr/lib64/ld-linux-x86-64.so.2",
            "/lib/ld-linux-aarch64.so.1",
        ):
            with self.subTest(path=path):
                self.assert_type(path, "openwave_elf_loader_t")

    def test_shared_objects(self):
        for path in (
            "/lib64/libc.so.6",
            "/usr/lib64/libselinux.so.1",
            "/usr/lib64/liblxc.so.1.8.0",
            "/usr/lib/x86_64-linux-gnu/libfoo.so.2",
            "/System/Applications/com.openwave.shell/lib/libQt6Core.so.6",
        ):
            with self.subTest(path=path):
                self.assert_type(path, "openwave_shared_lib_t")

    def test_existing_native_boundaries(self):
        mapping = {
            "/System/Services/appd": "openwave_runtime_t",
            "/System/Applications/com.openwave.shell/bin/ocean-launcher": "openwave_app_exec_t",
            "/Applications/com.example.app/bin/example": "openwave_app_exec_t",
            "/Data/Users/test/.DataLocation/Applications/com.example.app/data": "openwave_app_data_t",
            "/etc/selinux/openwave/policy/policy.35": "openwave_private_t",
            "/dev/null": "openwave_null_t",
        }
        # Service entrypoint mappings are generated after these static mappings
        # by build-openwave-selinux.py; the test above covers static source only.
        for path, expected in mapping.items():
            with self.subTest(path=path):
                self.assert_type(path, expected)


if __name__ == "__main__":
    unittest.main()
