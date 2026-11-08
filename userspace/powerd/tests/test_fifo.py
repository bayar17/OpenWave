#!/usr/bin/env python3
"""Exercise the deployed broker's FIFO checks without any host power action."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / 'toolchain/sysroot/x86_64-unknown-openwave'
with tempfile.TemporaryDirectory(prefix='openwave-power-fifo-') as temporary:
    directory = Path(temporary)
    source = directory / 'fifo.c'
    source.write_text(r'''
static const char *fixture_path;
#define POWER_FIFO_PATH fixture_path
#define POWER_OWNER_UID getuid()
#define main power_service_main
#include "''' + str(ROOT / 'userspace/powerd/main.c') + r'''"
#undef main
#include <assert.h>
int main(int argc, char **argv) {
    assert(argc == 2); fixture_path = argv[1];
    assert(open_init_fifo() == -1); /* absent */
    int file = open(fixture_path, O_CREAT | O_WRONLY, 0600);
    assert(file >= 0); close(file);
    assert(open_init_fifo() == -1); /* regular file */
    unlink(fixture_path);
    assert(symlink("/dev/null", fixture_path) == 0);
    assert(open_init_fifo() == -1); /* no symlinks */
    unlink(fixture_path);
    assert(mkfifo(fixture_path, 0600) == 0);
    assert(open_init_fifo() == -1); /* never blocks without a reader */
    int reader = open(fixture_path, O_RDONLY | O_NONBLOCK);
    assert(reader >= 0);
    assert(chmod(fixture_path, 0660) == 0);
    assert(open_init_fifo() == -1); /* protected owner-only command channel */
    assert(chmod(fixture_path, 0600) == 0);
    int writer = open_init_fifo(); assert(writer >= 0);
    assert(write(writer, "reboot", 6) == 6);
    char command[16] = {0}; assert(read(reader, command, sizeof(command)) == 6);
    assert(!strcmp(command, "reboot"));
    close(writer); close(reader);
    puts("POWER_FIFO PASS absent, regular, symlink, no-reader, insecure-mode and fixed command delivery");
    return 0;
}
''')
    includes = [ROOT / 'toolchain/include', SDK / 'usr/include/glib-2.0', SDK / 'usr/lib64/glib-2.0/include']
    binary = directory / 'fifo-test'
    subprocess.run([str(ROOT / 'toolchain/bin/clang'), '--target=x86_64-unknown-openwave',
        '--sysroot=' + str(SDK), '-D_GNU_SOURCE', '-Wno-macro-redefined',
        *['-I' + str(path) for path in includes], '-ffunction-sections', '-fdata-sections',
        '-Wl,--gc-sections', str(source), '-o', str(binary)], check=True)
    subprocess.run([str(SDK / 'lib64/ld-linux-x86-64.so.2'), '--library-path',
        str(SDK / 'usr/lib64') + ':' + str(SDK / 'lib64'), str(binary), str(directory / 'init.ctl')], check=True)
