"""Check production libselinux matching, including overlapping runtime rules."""
import ctypes as c
from pathlib import Path
import stat
import unittest

ROOT = Path(__file__).resolve().parents[4]
class Option(c.Structure):
    _fields_ = [('type', c.c_int), ('value', c.c_char_p)]

class FileContextsTests(unittest.TestCase):
    def test_specific_runtime_contexts_survive_supervisor_fallback(self):
        library = c.CDLL('libselinux.so.1')
        library.selabel_open.argtypes = [c.c_int, c.POINTER(Option), c.c_uint]
        library.selabel_open.restype = c.c_void_p
        library.selabel_lookup_raw.argtypes = [c.c_void_p, c.POINTER(c.c_char_p), c.c_char_p, c.c_int]
        library.freecon.argtypes = [c.c_char_p]
        library.selabel_close.argtypes = [c.c_void_p]
        path = str(ROOT / 'userspace/security/selinux/file_contexts').encode()
        options = (Option * 1)(Option(3, path))
        handle = library.selabel_open(0, options, 1)
        self.assertTrue(handle)
        cases = [
            ('/usr/lib/waydroid/data/scripts/waydroid-net.sh', stat.S_IFREG, 'openwave_waydroid_net_exec_t'),
            ('/run/waydroid-lxc/network_up', stat.S_IFREG, 'openwave_waydroid_net_runtime_t'),
            ('/run/xtables.lock', stat.S_IFREG, 'openwave_xtables_lock_t'),
            ('/var/lib/misc', stat.S_IFDIR, 'openwave_misc_t'),
            ('/var/lib/misc/dnsmasq.waydroid0.leases.new', stat.S_IFREG, 'openwave_waydroid_lease_t'),
            ('/var/lib/waydroid/images/system.img', stat.S_IFREG, 'openwave_waydroid_state_t'),
            ('/var/lib/waydroid-user/data', stat.S_IFDIR, 'openwave_waydroid_user_t'),
            ('/run/openwave/waydroid', stat.S_IFDIR, 'openwave_waydroid_snapshot_t'),
            ('/run/waydroid/manager.log', stat.S_IFREG, 'openwave_waydroid_runtime_t'),
            ('/Applications', stat.S_IFDIR, 'openwave_user_bundle_t'),
            ('/Applications/.webapp-install-test/manifest.json', stat.S_IFREG, 'openwave_user_bundle_t'),
            ('/Applications/org.openwave.webapp.test/bin/openwave-webapp', stat.S_IFREG, 'openwave_user_app_exec_t'),
            ('/System/Applications/com.openwave.ocean/bin/ocean', stat.S_IFREG, 'openwave_app_exec_t'),
            ('/run/openwave/apps', stat.S_IFDIR, 'openwave_app_storage_t'),
            ('/run/openwave/apps/fixture', stat.S_IFDIR, 'openwave_app_data_t'),
            ('/run/openwave/apps/fixture/tmp', stat.S_IFDIR, 'openwave_app_data_t'),
            ('/run/openwave/apps/fixture/shm/buffer', stat.S_IFREG, 'openwave_app_data_t'),
            ('/run/openwave/appd', stat.S_IFDIR, 'openwave_appd_runtime_t'),
            ('/run/openwave/appd/ready', stat.S_IFREG, 'openwave_appd_runtime_t'),
            ('/run/openwave/appd.pid.identity', stat.S_IFREG, 'openwave_service_run_t'),
            ('/run/openwave/ocean.pid.identity', stat.S_IFREG, 'openwave_service_run_t'),
            ('/run/openwave/public-preferences.ini', stat.S_IFREG, 'openwave_public_preferences_t'),
            ('/dev/rfkill', stat.S_IFCHR, 'openwave_rfkill_t'),
            ('/lib/firmware/rt2870.bin', stat.S_IFREG, 'openwave_firmware_t'),
            ('/lib/firmware/regulatory.db', stat.S_IFREG, 'openwave_firmware_t'),
            ('/run/openrc/init.ctl', stat.S_IFIFO, 'openwave_power_fifo_t'),
            ('/run/NetworkManager/resolv.conf', stat.S_IFREG, 'openwave_dns_config_t'),
            ('/run/NetworkManager/resolv.conf.tmp', stat.S_IFREG, 'openwave_dns_config_t'),
            ('/var/lib/NetworkManager/system-connections/private.nmconnection', stat.S_IFREG, 'openwave_networkmanager_state_t'),
            ('/run/user/1000/openwave-logs/ocean.stderr.log', stat.S_IFREG, 'openwave_qa_evidence_t'),
        ]
        try:
            for filename, mode, expected in cases:
                with self.subTest(filename=filename):
                    context = c.c_char_p()
                    self.assertEqual(library.selabel_lookup_raw(handle, c.byref(context), filename.encode(), mode), 0)
                    try:
                        self.assertEqual(context.value.decode(), f'system_u:object_r:{expected}:s0')
                    finally:
                        library.freecon(context)
        finally:
            library.selabel_close(handle)

if __name__ == '__main__':
    unittest.main()
