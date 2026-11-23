import importlib.util
import io
from pathlib import Path
import struct
import unittest
from unittest.mock import Mock, patch
import zipfile

spec = importlib.util.spec_from_file_location('install_apk', Path(__file__).resolve().parents[1] / 'install_apk.py')
apk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(apk)


def manifest():
    values = ['manifest', 'package', 'org.test.app', 'versionName', '1.0', apk.ANDROID_NAMESPACE]
    strings, offsets = b'', []
    for value in values:
        offsets.append(len(strings))
        raw = value.encode()
        strings += bytes([len(raw), len(raw)]) + raw + b'\0'
    start = 28 + 4 * len(values)
    pool = struct.pack('<HHIIIIII', 1, 28, start + len(strings), len(values), 0, 0x100, start, 0)
    pool += struct.pack('<' + 'I' * len(values), *offsets) + strings
    node = struct.pack('<HHIII', 0x102, 16, 76, 1, 0xffffffff)
    node += struct.pack('<IIHHHHHH', 0xffffffff, 0, 20, 20, 2, 0, 0, 0)
    for key, value in [(1, 2), (3, 4)]:
        node += struct.pack('<IIIHBBI', 0xffffffff if key == 1 else 5, key, value, 8, 0, 3, value)
    return struct.pack('<HHI', 3, 8, 8 + len(pool) + len(node)) + pool + node


def package(data=None):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w') as z:
        z.writestr('AndroidManifest.xml', manifest() if data is None else data)
        z.writestr('classes.dex', b'dex\n035\0')
    stream.seek(0)
    return stream


class ApkInstallTests(unittest.TestCase):
    def test_manifest_metadata(self):
        self.assertEqual(apk.inspect_apk(package()), ('org.test.app', '1.0', []))

    def test_decline_eof_and_wrong_answers_never_call_android(self):
        for answer in ['N\n', '', 'yes\n', 'Y', 'Y\n--yes']:
            connect, output = Mock(), []
            with self.subTest(answer=answer), self.assertRaisesRegex(ValueError, 'not authorized'):
                apk.authorize_and_install(package(), '/data/openwave-install/a.apk', 'a.apk', lambda: answer, output.append, connect)
            connect.assert_not_called()
            self.assertEqual(output[0].split('\t')[5], 'org.test.app')

    def test_explicit_authorization_calls_only_fixed_install_transaction(self):
        service, output = Mock(), []
        service.call.return_value.read_int32.return_value = (True, 0)
        apk.authorize_and_install(package(), '/data/openwave-install/a.apk', 'a.apk', lambda: 'Y\n', output.append, lambda: service)
        service.call.assert_called_once_with(5, '/data/openwave-install/a.apk')
        self.assertTrue(output[-1].startswith('R\tInstalled'))

    def test_android_rejection_is_failure(self):
        service, output = Mock(), []
        service.call.return_value.read_int32.return_value = (True, -2)
        with self.assertRaisesRegex(ValueError, 'rejected'):
            apk.authorize_and_install(package(), '/data/openwave-install/a.apk', 'a.apk', lambda: 'Y\n', output.append, lambda: service)
        self.assertFalse(any(s.startswith('R\t') for s in output))

    def test_malformed_package_never_prompts_or_calls_android(self):
        for stream in [io.BytesIO(b'garbage'), package(b'not XML'), package(b'<manifest package="org.test.app"/>')]:
            connect, output, answer = Mock(), [], Mock(return_value='Y\n')
            with self.assertRaises((ValueError, zipfile.BadZipFile, struct.error)):
                apk.authorize_and_install(stream, '/data/openwave-install/a.apk', 'a.apk', answer, output.append, connect)
            connect.assert_not_called(); answer.assert_not_called(); self.assertEqual(output, [])

    def test_no_command_line_bypass(self):
        for argv in [['helper', 'install-file', 'a.apk', '--yes'], ['helper', 'anything', 'a.apk']]:
            with patch.object(apk.os, 'geteuid', return_value=0), patch.object(apk.sys, 'argv', argv), self.assertRaises(ValueError):
                apk.main()
        with patch.object(apk.os, 'geteuid', return_value=1000), self.assertRaises(ValueError):
            apk.main()

    def test_truncated_manifests_fail(self):
        valid = manifest()
        for size in range(len(valid)):
            with self.assertRaises((ValueError, struct.error)):
                apk.manifest_info(valid[:size])


if __name__ == '__main__': unittest.main()
