#!/usr/bin/env python3
"""Verify private child output, bounded diagnostics and rejected storage paths."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SYSROOT = ROOT / 'toolchain/sysroot/x86_64-unknown-openwave'

class OutputTests(unittest.TestCase):
    def test_private_output_and_diagnostic_tail(self):
        with tempfile.TemporaryDirectory(prefix='openwave-output-') as temporary:
            temporary = Path(temporary)
            source, output = temporary / 'output.c', temporary / 'output'
            source.write_text(r'''
#include "process.c"
int main(void)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp("appd-output-XXXXXX", &error);
    g_assert_no_error(error);
    char *cache = g_build_filename(root, "Cache", NULL);
    g_assert_cmpint(mkdir(cache, 0700), ==, 0);
    App app = {.id = "org.openwave.test"};
    Instance instance = {.app = &app, .data = root, .uid = getuid(), .pid = 123,
                         .output_fds = {-1, -1}};
    g_assert_true(prepare_output(&instance, &error));
    g_assert_no_error(error);
    for (guint i = 0; i < 2; ++i) {
        struct stat st;
        g_assert_cmpint(fstat(instance.output_fds[i], &st), ==, 0);
        g_assert_cmpuint(st.st_nlink, ==, 0);
        g_assert_cmpuint(st.st_mode & 0777, ==, 0600);
        g_assert_cmpuint(st.st_uid, ==, getuid());
    }
    GDir *directory = g_dir_open(cache, 0, &error);
    g_assert_no_error(error);
    g_assert_null(g_dir_read_name(directory));
    g_dir_close(directory);
    char fill[9000]; memset(fill, 'x', sizeof(fill));
    g_assert_cmpint(write(instance.output_fds[1], fill, sizeof(fill)), ==, sizeof(fill));
    const char *message = "\nforged-line\t\"quoted\"\n";
    g_assert_cmpint(write(instance.output_fds[1], message, strlen(message)), ==, strlen(message));
    collect_output(&instance);
    g_assert_cmpint(instance.output_fds[0], ==, -1);
    g_assert_cmpint(instance.output_fds[1], ==, -1);
    collect_output(&instance); /* idempotent cleanup */
    g_assert_cmpint(rmdir(cache), ==, 0);
    g_assert_cmpint(symlink("/tmp", cache), ==, 0);
    g_assert_false(prepare_output(&instance, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
    g_clear_error(&error);
    unlink(cache); rmdir(root); g_free(cache); g_free(root);
    return 0;
}
''')
            subprocess.run([str(ROOT / 'toolchain/bin/clang'), '--target=x86_64-unknown-openwave',
                '--sysroot=' + str(SYSROOT), '-O2', '-ffunction-sections', '-fdata-sections',
                '-I' + str(ROOT / 'userspace/appd'), '-I' + str(ROOT / 'userspace/security/selinux'),
                '-I' + str(ROOT / 'toolchain/include'),
                '-I' + str(SYSROOT / 'usr/include/glib-2.0'),
                '-I' + str(SYSROOT / 'usr/lib64/glib-2.0/include'), str(source),
                '-Wl,--gc-sections', '-lgio-2.0', '-lgobject-2.0', '-lglib-2.0', '-o', str(output)], check=True)
            result = subprocess.run([str(SYSROOT / 'lib64/ld-linux-x86-64.so.2'), '--library-path',
                ':'.join(str(SYSROOT / p) for p in ('usr/lib64', 'usr/lib', 'lib64')), str(output)],
                check=True, capture_output=True, text=True)
            self.assertEqual(result.stderr.count('\n'), 1)
            self.assertTrue(result.stderr.startswith('appd: child appId=org.openwave.test pid=123 stderr="'))
            self.assertIn(r'\nforged-line\t\"quoted\"\n', result.stderr)
            self.assertLess(len(result.stderr), 8400)

if __name__ == '__main__':
    unittest.main()
