#!/usr/bin/env python3
"""Offline tests of canonical versions, source filtering and normalized archives."""
import importlib.util
import os
from pathlib import Path
import tempfile
import shutil
import struct
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('release', Path(__file__).resolve().parents[1] / 'scripts/release.py')
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleaseTest(unittest.TestCase):
    def test_versions(self):
        with tempfile.TemporaryDirectory() as temp, patch.object(release, 'ROOT', Path(temp)):
            file = Path(temp) / 'VERSION'
            for value in ('0.0.1\n', '12.3.4', '0.0.2\n'):
                file.write_text(value)
                self.assertEqual(release.version(), value.strip())
            for value in ('', '01.0.1', '0.1', '0.0.1\n0.0.2', 'v0.0.1', '1.2.3;echo oops'):
                file.write_text(value)
                with self.assertRaises(ValueError):
                    release.version()

    def test_normalized_archives(self):
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ, SOURCE_DATE_EPOCH='1700000000'):
            tree = Path(temp) / 'navi8or-test'
            tree.mkdir()
            (tree / 'nav').write_bytes(b'synthetic executable')
            (tree / 'nav').chmod(0o755)
            for suffix, writer in (('tar.gz', release.tar_archive), ('zip', release.zip_archive)):
                a, b = Path(temp) / ('a.' + suffix), Path(temp) / ('b.' + suffix)
                writer(tree, a)
                os.utime(tree / 'nav', (42, 42))
                writer(tree, b)
                self.assertEqual(a.read_bytes(), b.read_bytes())
                entries = release.package_entries(a)
                self.assertEqual(set(entries), {'navi8or-test/nav'})
                if suffix == 'tar.gz':
                    self.assertEqual(entries['navi8or-test/nav'][1], 0o755)

    def test_source_filter(self):
        with tempfile.TemporaryDirectory() as temp, patch.object(release, 'ROOT', Path(temp)), patch.object(release, 'OUT', Path(temp) / 'build/release'), patch.dict(os.environ, SOURCE_DATE_EPOCH='1700000000'):
            root = Path(temp)
            (root / 'VERSION').write_text('0.0.1\n')
            for name in release.DOCS + ('Makefile', '.gitignore'):
                (root / name).write_text('synthetic fixture\n')
            for name in ('.github', 'cmake', 'scripts', 'include', 'src', 'tests', 'themes', 'third_party', 'docs'):
                (root / name).mkdir()
            for name in ('tests/example.c', 'tests/example.py', 'tests/__pycache__/bad.pyc', 'tests/bad.o', 'tests/bad.exe', 'tests/editor~', 'nav', 'build/junk'):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('fixture')
            release.source()
            entries = release.package_entries(release.OUT / 'navi8or-source.tar.gz')
            self.assertIn('navi8or/tests/example.c', entries)
            self.assertIn('navi8or/tests/example.py', entries)
            self.assertFalse(any('bad.' in name or '__pycache__' in name or name.endswith('~') or '/build/' in name or name == 'navi8or/nav' for name in entries))

    def test_sanitisation_and_license_gate(self):
        self.assertIsNone(release.PERSONAL.search(b'Copyright (c) 2026 Ray Gibbon; password=test-pass; localhost'))
        self.assertIsNotNone(release.PERSONAL.search(b'/home/' + b'personal/src/project'))
        with tempfile.TemporaryDirectory() as temp, patch.object(release, 'ROOT', Path(temp)):
            (Path(temp) / 'VERSION').write_text('0.0.1\n')
            with self.assertRaises(ValueError):
                release.prerequisites()

    def test_staging_is_recreated(self):
        with tempfile.TemporaryDirectory() as temp, patch.object(release, 'OUT', Path(temp)):
            tree = release.fresh('linux')
            (tree / 'stale-file').write_text('must not ship')
            self.assertFalse((release.fresh('linux') / 'stale-file').exists())

    @unittest.skipUnless(shutil.which('x86_64-w64-mingw32-gcc'), 'MinGW unavailable')
    def test_strip_preserves_reproducible_pe_timestamp(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'main.c'
            binary = Path(temp) / 'test.exe'
            source.write_text('int main(void) { return 0; }\n')
            for value in ('0', '1700000000'):
                with patch.dict(os.environ, SOURCE_DATE_EPOCH=value):
                    release.run('x86_64-w64-mingw32-gcc', '-g', '-Wl,--no-insert-timestamp',
                                str(source), '-o', str(binary))
                    release.run('x86_64-w64-mingw32-strip', '--strip-all', str(binary))
                    data = binary.read_bytes()
                    pe = struct.unpack_from('<I', data, 0x3c)[0]
                    self.assertEqual(struct.unpack_from('<I', data, pe + 8)[0], int(value))


if __name__ == '__main__':
    unittest.main()
