#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path
import shutil
import sys

DIRECTORIES = ['bin', 'sbin', 'lib', 'lib64', 'plugins', 'qml', 'resources', 'resources/images', 'resources/sounds',
               'resources/fonts', 'share', 'share/icons', 'share/translations', 'share/data']


def main():
    rootfs, url, name = Path(sys.argv[1]), sys.argv[2], sys.argv[3]
    app_id = 'org.openwave.webapp.h' + hashlib.sha256(url.encode()).hexdigest()
    bundle = rootfs / 'Applications' / app_id
    if bundle.exists():
        shutil.rmtree(bundle)
    for directory in DIRECTORIES:
        (bundle / directory).mkdir(parents=True, mode=0o755)
    shutil.copy2(rootfs / 'usr/libexec/openwave-webapp', bundle / 'bin/openwave-webapp')
    (bundle / 'bin/openwave-webapp').chmod(0o755)
    shutil.copy2(rootfs / 'usr/share/pixmaps/openwave-webapp.svg', bundle / 'resources/icon.svg')
    (bundle / 'resources/icon.svg').chmod(0o644)
    manifest = {'schemaVersion': 1, 'id': app_id, 'name': name, 'version': '1.0', 'runtime': 'openwave-qt',
                'executable': 'bin/openwave-webapp', 'sandbox': {'enabled': True, 'profile': 'standard'},
                'permissions': [], 'singleInstance': True, 'acceptsPaths': False, 'icon': 'resources/icon.svg',
                'webApp': {'url': url}}
    toml = (f'schema = 1\nid = "{app_id}"\nname = "{name}"\nversion = "1.0"\nkind = "application"\n'
            'executable = "bin/openwave-webapp"\nruntime = "openwave-qt"\nicon = "resources/icon.svg"\n'
            f'single_instance = true\naccepts_paths = false\n\n[sandbox]\nprofile = "standard"\n\n[permissions]\nrequest = []\n\n[web_app]\nurl = "{url}"\n')
    (bundle / 'manifest.json').write_text(json.dumps(manifest, indent=4) + '\n')
    (bundle / 'Application.toml').write_text(toml)
    for path in (bundle / 'manifest.json', bundle / 'Application.toml'):
        path.chmod(0o644)
    bundle.chmod(0o755)
    print(app_id)


main()
