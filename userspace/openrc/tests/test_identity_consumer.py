"""Exercise the production identity reader against protected registrar records."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SDK = Path(os.environ.get('OPENWAVE_SDK', ROOT / 'toolchain'))
TRIPLE = os.environ.get('OPENWAVE_TARGET_TRIPLE', 'x86_64-unknown-openwave')
SYSROOT = SDK / 'sysroot' / TRIPLE


class IdentityConsumerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='openwave-identity-consumer-')
        cls.base = Path(cls.temporary.name)
        cls.proc = cls.base / 'proc'
        cls.pid = 4123
        process = cls.proc / str(cls.pid)
        process.mkdir(parents=True)
        fields = ['S'] + ['1'] * 18 + ['98765']
        (process / 'stat').write_text(f'{cls.pid} (fixture daemon) ' + ' '.join(fields) + '\n')
        cls.pidfile = cls.base / 'ocean.pid'
        cls.marker = cls.pidfile.with_suffix('.pid.identity')
        harness = cls.base / 'consumer.c'
        harness.write_text('''#include "OpenWaveIdentity.h"
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
    if (argc != 6) return 2;
    guint uid = strtoul(argv[2], NULL, 10), pid = strtoul(argv[3], NULL, 10);
    guint expected_uid = strtoul(argv[5], NULL, 10);
    gboolean okay = !strcmp(argv[1], "default")
        ? openwave_supervised_process(uid, pid, argv[4])
        : openwave_supervised_process_uid(uid, pid, argv[4], expected_uid);
    return okay ? 0 : 13;
}
''')
        cls.binary = cls.base / 'consumer'
        labels = cls.base / 'labels.c'
        labels.write_text('''#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
int is_selinux_enabled(void) { return 1; }
int fgetfilecon_raw(int fd, char **context)
{
    (void)fd;
    const char *label = getenv("IDENTITY_TEST_LABEL");
    if (label && !strcmp(label, "error")) return -1;
    *context = strdup(label ? label : "system_u:object_r:openwave_service_run_t:s0");
    return *context ? (int)strlen(*context) : -1;
}
void freecon(char *context) { free(context); }
''')
        subprocess.run([
            str(SDK / 'bin/clang'), '--target=' + TRIPLE, '--sysroot=' + str(SYSROOT),
            '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-I' + str(SYSROOT / 'usr/include/glib-2.0'),
            '-I' + str(SYSROOT / 'usr/lib64/glib-2.0/include'),
            '-I' + str(ROOT / 'userspace/security'),
            '-DOPENWAVE_PROC_DIR="' + str(cls.proc) + '"',
            '-DOPENWAVE_OWNER_UID=' + str(os.geteuid()),
            str(harness), str(labels), str(ROOT / 'userspace/security/OpenWaveIdentity.c'),
            '-lgio-2.0', '-lgobject-2.0', '-lglib-2.0', '-o', str(cls.binary),
        ], check=True)
        cls.launch = [str(SYSROOT / 'lib64/ld-linux-x86-64.so.2'), '--library-path',
                      ':'.join(str(SYSROOT / path) for path in
                               ('usr/lib64', 'lib64', 'usr/lib', 'lib')), str(cls.binary)]

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def record(self, text):
        self.marker.unlink(missing_ok=True)
        self.marker.write_text(text)
        self.marker.chmod(0o600)

    def accepted(self, uid, expected_uid=0, default=False, pid=None):
        result = subprocess.run(self.launch + ['default' if default else 'explicit', str(uid),
            str(self.pid if pid is None else pid), str(self.pidfile), str(expected_uid)],
            capture_output=True, text=True)
        self.assertIn(result.returncode, (0, 13), result.stderr)
        return result.returncode == 0

    def test_root_services_keep_root_only_default(self):
        self.record(f'{self.pid} 0 98765\n')
        self.assertTrue(self.accepted(0, default=True))
        self.assertTrue(self.accepted(0, expected_uid=0))
        self.assertFalse(self.accepted(1000, default=True))
        self.assertFalse(self.accepted(1000, expected_uid=1000))

    def test_ocean_requires_explicit_matching_uid(self):
        self.record(f'{self.pid} 1000 98765\n')
        self.assertTrue(self.accepted(1000, expected_uid=1000))
        self.assertFalse(self.accepted(1000, default=True))
        self.assertFalse(self.accepted(0, expected_uid=1000))
        self.assertFalse(self.accepted(0, default=True))
        self.assertFalse(self.accepted(1001, expected_uid=1000))
        self.assertFalse(self.accepted(1000, expected_uid=1001))

    def test_malformed_stale_or_wrong_pid_records_fail_closed(self):
        for record in (f'{self.pid} 98765\n', f'{self.pid} 0\n',
                       f'{self.pid} 0 98765 extra\n', f'{self.pid} 0 98765 9\n',
                       f'{self.pid} 0 0\n', f'{self.pid} 0 98766\n',
                       f'{self.pid + 1} 0 98765\n', f'{self.pid} 1000 98765\n', ''):
            with self.subTest(record=record):
                self.record(record)
                self.assertFalse(self.accepted(0, default=True))
        self.record(f'{self.pid} 0 98765\n')
        self.assertFalse(self.accepted(0, default=True, pid=self.pid + 1))

    def test_unprotected_or_symlinked_marker_is_rejected(self):
        self.record(f'{self.pid} 0 98765\n')
        self.marker.chmod(0o622)
        self.assertFalse(self.accepted(0, default=True))
        target = self.base / 'forged-identity'
        target.write_text(f'{self.pid} 0 98765\n')
        target.chmod(0o600)
        self.marker.unlink()
        self.marker.symlink_to(target)
        self.assertFalse(self.accepted(0, default=True))

    def test_service_owned_file_cannot_forge_supervisor_record(self):
        self.record(f'{self.pid} 0 98765\n')
        self.assertTrue(self.accepted(0, default=True))
        for label in ('system_u:object_r:openwave_public_preferences_t:s0',
                      'system_u:object_r:openwave_appd_runtime_t:s0', 'error'):
            with self.subTest(label=label):
                previous = os.environ.get('IDENTITY_TEST_LABEL')
                os.environ['IDENTITY_TEST_LABEL'] = label
                try:
                    self.assertFalse(self.accepted(0, default=True))
                finally:
                    if previous is None: os.environ.pop('IDENTITY_TEST_LABEL', None)
                    else: os.environ['IDENTITY_TEST_LABEL'] = previous


if __name__ == '__main__':
    unittest.main()
