#!/usr/bin/env python3
"""Build against the target SDK and run account transactions under fakeroot."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SYSROOT = ROOT / "toolchain/sysroot/x86_64-unknown-openwave"

with tempfile.TemporaryDirectory(prefix="openwave-account-test-") as temporary:
    output = Path(temporary) / "accounts-test"
    subprocess.run([
        str(ROOT / "toolchain/bin/clang"), "--target=x86_64-unknown-openwave",
        f"--sysroot={SYSROOT}", "-ffunction-sections", "-fdata-sections",
        "-Wall", "-Wextra", "-Werror=implicit-function-declaration",
        f"-I{SYSROOT}/usr/include/glib-2.0",
        f"-I{SYSROOT}/usr/lib64/glib-2.0/include",
        f"-I{ROOT}/userspace/security/selinux",
        str(ROOT / "userspace/appd/tests/accounts.c"),
        "-Wl,--gc-sections", "-lgio-2.0", "-lgobject-2.0", "-lglib-2.0",
        "-o", str(output),
    ], check=True)
    environment = os.environ | {"OPENWAVE_ACCOUNT_TEST_ROOT": temporary}
    subprocess.run([
        "fakeroot", "--", "/bin/sh", "-c",
        'exec "$1" --library-path "$2:$LD_LIBRARY_PATH" "$3"', "_",
        str(SYSROOT / "lib64/ld-linux-x86-64.so.2"),
        f"{SYSROOT}/usr/lib64:{SYSROOT}/usr/lib:{SYSROOT}/lib64",
        str(output),
    ], env=environment, check=True)
    # Verify the exact installed symlink layout remains usable through a
    # narrow readonly backing bind, without exposing private identity state.
    if not shutil.which("bwrap"):
        raise SystemExit("bwrap is required for the NSS namespace regression")
    fixture = Path(temporary) / "sandbox"
    (fixture / "etc").mkdir(parents=True)
    (fixture / "accounts").mkdir()
    passwd = fixture / "accounts/passwd"
    passwd.write_text("root:x:0:0:root:/root:/bin/sh\n"
                      "owapp200000:x:200000:200000:OpenWave application:/app:/bin/false\n")
    (fixture / "etc/passwd").symlink_to("../var/lib/openwave-accounts/passwd")
    command = ["bwrap", "--unshare-user", "--uid", "0", "--gid", "0",
               "--die-with-parent", "--dir", "/var/lib"]
    for path in ("/usr", "/lib", "/lib64"):
        if Path(path).exists():
            command += ["--ro-bind", path, path]
    command += ["--ro-bind", str(fixture / "etc"), "/etc",
                "--ro-bind", str(fixture / "accounts"), "/var/lib/openwave-accounts",
                "--ro-bind", str(output), "/probe",
                "--ro-bind", str(SYSROOT), "/sdk",
                "--", "/sdk/lib64/ld-linux-x86-64.so.2", "--library-path",
                "/sdk/usr/lib64:/sdk/usr/lib:/sdk/lib64", "/probe", "--sandbox-probe"]
    subprocess.run(command, check=True)
    assert passwd.read_text().endswith("/app:/bin/false\n")
    print("PASS: sandbox libc resolves the passwd backing; both paths reject writes")
