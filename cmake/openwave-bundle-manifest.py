#!/usr/bin/env python3
import json
import re
import sys
import tomllib

TOP = {'schema', 'id', 'name', 'version', 'kind', 'executable', 'icon', 'runtime', 'single_instance',
       'accepts_paths', 'arguments', 'sandbox', 'permissions', 'opens', 'mime_types', 'developer',
       'categories', 'uri_schemes', 'comment'}
IDENTIFIER = re.compile(r'^[a-zA-Z][a-zA-Z0-9_-]*(?:\.[a-zA-Z0-9][a-zA-Z0-9_-]*){2,}$')
PERMISSION = re.compile(r'^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$')
REQUIRES = {'landlock', 'uid', 'cgroups-v2', 'selinux'}
EXTENSION = re.compile(r'^[a-z0-9]{1,16}$')
CATEGORY = re.compile(r'^[A-Za-z][A-Za-z0-9-]{0,31}$')
SCHEME = re.compile(r'^[a-z][a-z0-9+.-]{0,31}$')
LAUNCHER = '/System/Services/openwave-launch'


def relative(path):
    return bool(path) and not path.startswith('/') and '\\' not in path and '\0' not in path \
        and all(part not in ('', '.', '..') for part in path.split('/'))


def convert(text, system=False):
    document = tomllib.loads(text)
    problems = []
    for key in document:
        if key not in TOP and not (system and key == 'role'):
            problems.append(f'unknown key "{key}"')
    if document.get('schema') != 1:
        problems.append('schema must be 1')
    for key in ('id', 'name', 'version'):
        if not isinstance(document.get(key), str) or not document.get(key):
            problems.append(f'{key} is required')
    identifier = document.get('id', '')
    if not IDENTIFIER.match(identifier) or len(identifier) > 128:
        problems.append('id is not a valid application ID')
    if not system and identifier.startswith(('com.openwave.', 'org.openwave.android.', 'org.openwave.webapp.')):
        problems.append('id is in a reserved namespace')
    if system and not identifier.startswith('com.openwave.'):
        problems.append('a system bundle must use the com.openwave. namespace')
    kind = document.get('kind', 'application')
    if kind not in ('application', 'library'):
        problems.append('kind must be "application" or "library"')
    executable = document.get('executable', '')
    if kind == 'application':
        if not relative(executable) or not executable.startswith('bin/') or len(executable) <= 4:
            problems.append('executable must be a relative path below bin/')
    elif executable:
        problems.append('a library has no executable')
    runtime = document.get('runtime', 'openwave-qt')
    if runtime != 'openwave-qt':
        problems.append('runtime must be "openwave-qt"')
    sandbox = document.get('sandbox', {})
    if not isinstance(sandbox, dict) or set(sandbox) - {'profile', 'requires'}:
        problems.append('sandbox accepts only profile and requires')
        sandbox = {}
    if sandbox.get('profile', 'standard') != 'standard':
        problems.append('sandbox.profile must be "standard"')
    requires = sandbox.get('requires', [])
    if not isinstance(requires, list) or any(item not in REQUIRES for item in requires):
        problems.append('unsupported sandbox requirement')
    permissions = document.get('permissions', {})
    if not isinstance(permissions, dict) or set(permissions) - {'request'}:
        problems.append('permissions accepts only request')
        permissions = {}
    requested = permissions.get('request', [])
    if not isinstance(requested, list) or len(requested) > 64 or len(set(requested)) != len(requested) \
            or any(not isinstance(item, str) or not PERMISSION.match(item) for item in requested):
        problems.append('invalid permission list')
    icon = document.get('icon', '')
    if icon and not relative(icon):
        problems.append('icon must be a relative path inside the bundle')
    arguments = document.get('arguments', [])
    if not isinstance(arguments, list) or len(arguments) > 16 or any(not isinstance(a, str) or len(a) > 4096 for a in arguments):
        problems.append('invalid arguments')
    opens = document.get('opens', [])
    if not isinstance(opens, list) or len(opens) > 32 or len(set(opens)) != len(opens) \
            or any(not isinstance(item, str) or not EXTENSION.match(item) for item in opens):
        problems.append('opens must list at most 32 distinct lowercase file extensions')
    elif opens and not document.get('accepts_paths', False):
        problems.append('opens requires accepts_paths = true')
    mime_types = document.get('mime_types', [])
    mime = re.compile(r'[a-z0-9][a-z0-9!#$&^_.+-]{0,126}/[a-z0-9][a-z0-9!#$&^_.+-]{0,126}')
    if not isinstance(mime_types, list) or len(mime_types) > 64 or any(not isinstance(t, str) or not mime.fullmatch(t) for t in mime_types):
        problems.append('invalid MIME types')
    elif len(set(mime_types)) != len(mime_types) or (mime_types and not document.get('accepts_paths', False)):
        problems.append('MIME types must be unique and require accepts_paths')
    developer = document.get('developer', '')
    if not isinstance(developer, str) or len(developer) > 256 or any(ord(c) < 32 or ord(c) == 127 for c in developer):
        problems.append('invalid developer')
    categories = document.get('categories', [])
    if not isinstance(categories, list) or len(categories) > 8 or len(set(categories)) != len(categories) \
            or any(not isinstance(item, str) or not CATEGORY.match(item) for item in categories):
        problems.append('categories must list at most 8 distinct Freedesktop category names')
    schemes = document.get('uri_schemes', [])
    if not isinstance(schemes, list) or len(schemes) > 16 or len(set(schemes)) != len(schemes) \
            or any(not isinstance(item, str) or not SCHEME.match(item) for item in schemes):
        problems.append('uri_schemes must list at most 16 distinct lowercase URI schemes')
    elif schemes and not document.get('accepts_paths', False):
        problems.append('uri_schemes requires accepts_paths = true')
    comment = document.get('comment', '')
    if not isinstance(comment, str) or len(comment) > 256 or any(ord(c) < 32 or ord(c) == 127 for c in comment):
        problems.append('invalid comment')
    if problems:
        raise ValueError('; '.join(problems))
    manifest = {'schemaVersion': 1, 'id': identifier, 'name': document['name'], 'version': document['version'],
                'type': 'system' if system else 'application', 'executable': executable, 'runtime': runtime}
    manifest['developer'] = developer
    manifest['mimeTypes'] = mime_types
    if icon:
        manifest['icon'] = icon
    manifest['sandbox'] = {'enabled': True, 'profile': 'standard', **({'requires': requires} if requires else {})}
    manifest['permissions'] = requested
    manifest['singleInstance'] = bool(document.get('single_instance', True))
    manifest['acceptsPaths'] = bool(document.get('accepts_paths', False))
    if arguments:
        manifest['arguments'] = arguments
    if opens:
        manifest['opens'] = opens
    if categories:
        manifest['categories'] = categories
    if schemes:
        manifest['uriSchemes'] = schemes
    if comment:
        manifest['comment'] = comment
    if system and 'role' in document:
        manifest['role'] = document['role']
    return json.dumps(manifest, indent=2) + '\n'


def escape(value):
    return ''.join(' ' if ord(c) < 32 or ord(c) == 127 else c for c in value).replace('\\', '\\\\')


def desktop_entry(manifest_text):
    manifest = json.loads(manifest_text)
    if not manifest.get('executable'):
        return None
    root = '/System/Applications' if manifest.get('type') == 'system' else '/Applications'
    identifier = manifest['id']
    mimes = list(manifest.get('mimeTypes', [])) + ['x-scheme-handler/' + item for item in manifest.get('uriSchemes', [])]
    suffix = ' %F' if manifest.get('acceptsPaths') and (manifest.get('mimeTypes') or manifest.get('opens')) \
        else ' %U' if manifest.get('uriSchemes') else ' %F' if manifest.get('acceptsPaths') else ''
    lines = ['[Desktop Entry]', 'Type=Application', 'Version=1.0', f"Name={escape(manifest['name'])}"]
    if manifest.get('comment'):
        lines.append(f"Comment={escape(manifest['comment'])}")
    lines.append(f'Exec={LAUNCHER} {identifier}{suffix}')
    if manifest.get('icon'):
        lines.append(f"Icon={root}/{identifier}/{manifest['icon']}")
    lines.append('Terminal=false')
    if manifest.get('categories'):
        lines.append('Categories=' + ';'.join(manifest['categories']) + ';')
    if mimes:
        lines.append('MimeType=' + ';'.join(mimes) + ';')
    lines.append(f'X-OpenWave-AppId={identifier}')
    if manifest.get('acceptsPaths'):
        lines.append('X-OpenWave-AcceptsPaths=true')
    return '\n'.join(lines) + '\n'


def main(argv):
    system = '--system' in argv
    desktop = None
    args = [a for a in argv if a != '--system']
    if '--desktop' in args:
        index = args.index('--desktop')
        if index + 1 >= len(args):
            print('--desktop needs an output path', file=sys.stderr)
            return 2
        desktop = args[index + 1]
        del args[index:index + 2]
    if len(args) != 2:
        print('usage: openwave-bundle-manifest.py Application.toml OUTPUT [--system] [--desktop DESKTOP_OUTPUT]', file=sys.stderr)
        return 2
    try:
        with open(args[0], encoding='utf-8') as source:
            text = source.read()
        output = convert(text, system)
    except (OSError, ValueError, tomllib.TOMLDecodeError) as error:
        print(f'{args[0]}: {error}', file=sys.stderr)
        return 1
    with open(args[1], 'w', encoding='utf-8') as destination:
        destination.write(output)
    if desktop:
        entry = desktop_entry(output)
        if entry:
            with open(desktop, 'w', encoding='utf-8') as destination:
                destination.write(entry)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
