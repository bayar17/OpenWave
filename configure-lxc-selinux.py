from pathlib import Path
from datetime import datetime
import re

root = Path("/mnt/aosp/ThunderOS")
policy = root / "userspace/security/selinux/openwave.cil"
contexts = root / "userspace/security/selinux/file_contexts"

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")

# Backups
for p in (policy, contexts):
    backup = p.with_name(p.name + ".before-lxc-" + stamp)
    backup.write_bytes(p.read_bytes())
    print("BACKUP:", backup)

cil = policy.read_text()
fc = contexts.read_text()

# All installed LXC commands, not just lxc-start.
executables = []
for directory in ("usr/bin", "usr/sbin", "bin", "sbin"):
    d = root / "rootfs" / directory
    if d.exists():
        for p in sorted(d.glob("lxc-*")):
            if p.is_file() or p.is_symlink():
                executables.append("/" + str(p.relative_to(root / "rootfs")))

print("\nInstalled LXC executables:")
for p in sorted(set(executables)):
    print(" ", p)

if not executables:
    raise SystemExit("No LXC executables found; policy not changed.")

types = [
    "openwave_lxc_exec_t",
    "openwave_lxc_run_t",
]

for t in types:
    declaration = f"(type {t})"
    if not re.search(r"\(type\s+" + re.escape(t) + r"\s*\)", cil):
        cil += "\n" + declaration

rules = [
    # The Waydroid manager retains its existing process domain.
    """(allow openwave_waydroid_manager_t openwave_lxc_exec_t
       (file (read open getattr map execute execute_no_trans)))""",

    # LXC lock and runtime files.
    """(allow openwave_waydroid_manager_t openwave_lxc_run_t
       (dir (create read write open search getattr add_name remove_name)))""",

    """(allow openwave_waydroid_manager_t openwave_lxc_run_t
       (file (create read write open getattr setattr unlink lock)))""",

    # OpenRC creates the initial runtime directory under /run.
    """(typetransition openwave_system_t openwave_run_t
       dir "lxc" openwave_lxc_run_t)""",

    """(allow openwave_system_t openwave_lxc_run_t
       (dir (create read write open search getattr setattr add_name remove_name)))""",

    """(allow openwave_system_t openwave_lxc_run_t
       (file (create read write open getattr setattr unlink lock)))""",
]

def normalized(s):
    return re.sub(r"\s+", " ", s).strip()

for rule in rules:
    if normalized(rule) not in normalized(cil):
        cil += "\n" + rule + "\n"

# Exact installed executable paths, avoiding broad helper relabeling.
for path in sorted(set(executables)):
    regex = re.escape(path)
    entry = (
        regex +
        " system_u:object_r:openwave_lxc_exec_t:s0"
    )
    if entry not in fc:
        fc += "\n" + entry

runtime = (
    "/run/lxc(/.*)? "
    "system_u:object_r:openwave_lxc_run_t:s0"
)
if runtime not in fc:
    fc += "\n" + runtime

policy.write_text(cil.rstrip() + "\n")
contexts.write_text(fc.rstrip() + "\n")

print("\nLXC SELinux configuration updated.")
print("No global privileged LXC domain transition added.")
print("Compile policy and verify labels before booting.")
