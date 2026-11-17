import os
from pathlib import PurePosixPath
import re

QT_WRAPPERS = ('qmake', 'qmake6', 'qtpaths', 'qtpaths6')


def climb(from_directory):
    depth = len(PurePosixPath(from_directory).parts) if from_directory not in ('', '.') else 0
    return '/'.join(['..'] * depth) or '.'


def rewrite(relative, data, prefixes):
    if not any(prefix.encode() in data for prefix in prefixes):
        return data
    text = data.decode('utf-8')
    path = PurePosixPath(relative)
    up = climb(path.parent.as_posix())
    alternation = '|'.join(re.escape(prefix) for prefix in sorted(prefixes, key=len, reverse=True))
    pattern = re.compile('(?:' + alternation + ')')
    if path.suffix == '.pc':
        text = pattern.sub('${pcfiledir}/' + up, text)
    elif path.suffix == '.cmake':
        text = re.sub(r'^set\((__qt_platform_initial_qt_host_path\w*) "(?:' + alternation + r')([^"]*)"\)',
                      lambda m: f'get_filename_component({m.group(1)} "${{CMAKE_CURRENT_LIST_DIR}}/{up}{m.group(2)}" ABSOLUTE)',
                      text, flags=re.M)
        text = pattern.sub('${CMAKE_CURRENT_LIST_DIR}/' + up, text)
    elif path.suffix == '.pri':
        text = pattern.sub('$$PWD/' + up, text)
    elif path.suffix == '.spdx':
        text = pattern.sub('@SDK@', text)
    elif path.name in QT_WRAPPERS:
        text = text.replace('script_dir_path=`(cd "$script_dir_path"; pwd)`\n',
                            'script_dir_path=`(cd "$script_dir_path"; pwd)`\n'
                            f'sdk_root=`(cd "$script_dir_path/{up}"; pwd)`\n', 1)
        text = pattern.sub('$sdk_root', text)
        text = re.sub(r'^(\$sdk_root/bin/\w+) -qtconf "\$script_dir_path/target_qt.conf" \$\*$',
                      'configuration=`mktemp`\n'
                      'sed "s#@OPENWAVE_SDK@#$sdk_root#g" "$script_dir_path/target_qt.conf" > "$configuration"\n'
                      r'\1 -qtconf "$configuration" "$@"' '\n'
                      'status=$?\nrm -f "$configuration"\nexit $status',
                      text, flags=re.M)
    elif path.name == 'target_qt.conf':
        text = pattern.sub('@OPENWAVE_SDK@', text)
    elif path.name == 'curl-config':
        text = re.sub(r"echo '(?:" + alternation + r")/bin/clang'",
                      f'echo "$(cd "$(dirname "$0")/{up}" && pwd)/bin/clang"', text)
    else:
        raise ValueError(f'no rule for making {relative} independent of its location')
    result = text.encode('utf-8')
    if any(prefix.encode() in result for prefix in prefixes):
        raise ValueError(f'{relative} still names the build location after rewriting')
    return result
