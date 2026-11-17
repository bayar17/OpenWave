#!/usr/bin/env python3
"""Offline tests for Ocean KMS access and render-only app sandbox devices."""

from pathlib import Path
import re
import shutil
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[3]
RULES = ROOT / "userspace/openrc/udev/70-openwave-drm.rules"
OCEAN_SERVICE = ROOT / "userspace/openrc/etc/init.d/openwave-ocean"
UDEVD_SERVICE = ROOT / "userspace/openrc/etc/init.d/udevd"
BUILDER = ROOT / "scripts/prepare-openwave-boot.py"
NAMESPACE = ROOT / "userspace/appd/namespace.c"
PROCESS = ROOT / "userspace/appd/process.c"
OPENRC_SSD = ROOT / "third_party/openrc-0.64.1/src/start-stop-daemon/start-stop-daemon.c"
KMS = ROOT / "userspace/openrc/kms.json"
GENERAL_KMS = ROOT / "userspace/config/kms.json"
ROOTFS_KMS = ROOT / "rootfs/etc/openwave/kms.json"


class DrmDevicePolicyTests(unittest.TestCase):
    def test_eudev_rules_set_restricted_primary_and_render_modes(self):
        rules = RULES.read_text()
        self.assertRegex(rules, r'(?m)^SUBSYSTEM=="drm", KERNEL=="card\[0-9\]\*", GROUP="video", MODE="0660"$')
        self.assertRegex(rules, r'(?m)^SUBSYSTEM=="drm", KERNEL=="renderD\[0-9\]\*", GROUP="render", MODE="0660"$')
        self.assertNotIn('MODE="0666"', rules)
        builder = BUILDER.read_text()
        self.assertIn('userspace/openrc/udev/70-openwave-drm.rules', builder)
        self.assertIn('etc/udev/rules.d/70-openwave-drm.rules', builder)
        verifier = shutil.which("udevadm")
        if verifier:
            subprocess.run([verifier, "verify", str(RULES)], check=True)

    def test_udevd_resolves_named_drm_groups_early(self):
        service = UDEVD_SERVICE.read_text()
        self.assertIn('command_args="--resolve-names=early"', service)
        self.assertNotIn("--resolve-names=never", service)

        groups = [line.split(":") for line in (ROOT / "rootfs/etc/group").read_text().splitlines()]
        by_name = {entry[0]: entry for entry in groups}
        self.assertEqual(by_name["video"][2:], ["100", "ocean"])
        self.assertEqual(by_name["render"][2:], ["102", "ocean"])

    def test_ocean_waits_for_coldplug_and_stays_unprivileged(self):
        service = OCEAN_SERVICE.read_text()
        dependency = re.search(r"(?ms)^depend\(\) \{(.*?)^\}", service)
        self.assertIsNotNone(dependency)
        self.assertRegex(dependency.group(1), r"(?m)^\s*need\s+.*\budevd\b")
        self.assertIn('command_user="ocean:ocean"', service)
        self.assertIn('no_new_privs="yes"', service)
        self.assertNotIn('command_user="root', service)

    def test_ocean_supplementary_groups_are_applied_before_uid_drop(self):
        source = OPENRC_SSD.read_text()
        self.assertIn("getgrouplist(changeuser, gid, group_list, &group_count)", source)
        setgroups = source.index("setgroups(group_count, group_list)")
        setuid = source.index("if (uid && cap_setuid(uid))")
        self.assertLess(setgroups, setuid)
        groups = (ROOT / "rootfs/etc/group").read_text().splitlines()
        self.assertIn("video:x:100:ocean", groups)
        self.assertIn("render:x:102:ocean", groups)
        passwd = [line.split(":") for line in (ROOT / "rootfs/etc/passwd").read_text().splitlines()
                  if line.startswith("ocean:")]
        self.assertEqual(len(passwd), 1)
        self.assertEqual(passwd[0][2:4], ["1000", "1000"])

    def test_sandbox_binds_only_validated_render_nodes(self):
        source = NAMESPACE.read_text()
        self.assertIn('const char *devices[] = {"/dev/null", "/dev/zero", "/dev/random", "/dev/urandom", NULL};', source)
        self.assertIn("bind_render_nodes(args);", source)
        self.assertIn('open("/dev/dri", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)', source)
        self.assertIn("fstatat(dirfd(entries), entry->d_name, &node, AT_SYMLINK_NOFOLLOW)", source)
        self.assertIn("!S_ISCHR(node.st_mode) || major(node.st_rdev) != 226", source)
        self.assertIn('mount_path(args, "--dev-bind-try", path, path);', source)
        self.assertNotRegex(source, r'"/dev/dri"\s*,\s*NULL')
        process = PROCESS.read_text()
        self.assertIn("setgroups(render ? 1 : 0, render ? &render : NULL)", process)
        self.assertNotIn("setgroups(video", process)

    def test_kms_configuration_uses_primary_node_not_render_node(self):
        import json
        for path in (KMS, GENERAL_KMS, ROOTFS_KMS):
            with self.subTest(path=path):
                config = json.loads(path.read_text())
                self.assertEqual(config.get("device"), "/dev/dri/card0")
                self.assertTrue(any(output.get("name") == "Virtual1" for output in config.get("outputs", [])))
                self.assertFalse(any(output.get("name") == "Virtual-1" for output in config.get("outputs", [])))

    def test_openrc_shell_reads_the_pinned_kms_configuration(self):
        service = OCEAN_SERVICE.read_text()
        self.assertIn('export QT_QPA_EGLFS_KMS_CONFIG="/etc/openwave/kms.json"', service)
        self.assertIn('export QT_QPA_EGLFS_INTEGRATION="eglfs_kms"', service)



if __name__ == "__main__":
    unittest.main()
