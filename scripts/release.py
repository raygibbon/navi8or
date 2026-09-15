#!/usr/bin/env python3
"""Explicit, normalized release staging and checks; no network or publication."""
import argparse
import gzip
import hashlib
import io
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'build/release'
DOCS = ('README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md')
PERSONAL = re.compile(rb'r' + rb'gibbon|/(?:var/)?home/[^/\s]+/|Sync/git|Ray' + rb' DOS')


def run(*args, cwd=ROOT):
    # BFD strip/objcopy otherwise replace a zero PE timestamp with current time.
    env = dict(os.environ, SOURCE_DATE_EPOCH=str(epoch()))
    return subprocess.check_output(args, cwd=cwd, env=env, stderr=subprocess.STDOUT).decode()


def version():
    data = (ROOT / 'VERSION').read_text()
    if not re.fullmatch(r'(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\n?', data):
        raise ValueError('VERSION must contain one semantic numeric version')
    return data.strip()


def epoch():
    value = os.environ.get('SOURCE_DATE_EPOCH')
    if value is None:
        try:
            value = subprocess.check_output(['git', 'log', '-1', '--format=%ct'], cwd=ROOT,
                                            stderr=subprocess.DEVNULL).decode().strip()
        except subprocess.CalledProcessError:
            value = '0'
    return int(value)


def prerequisites():
    version()
    for name in DOCS:
        if not (ROOT / name).is_file() or not (ROOT / name).stat().st_size:
            raise ValueError(f'{name} must exist before release')


def fresh(name):
    path = OUT / 'stage' / name
    # This fixed generated child is owned by this script, never the checkout.
    if path.exists():
        if path.is_symlink():
            raise ValueError('refusing symlink staging directory')
        shutil.rmtree(path)
    path.mkdir(parents=True)
    return path


def tar_archive(tree, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open('wb') as raw, gzip.GzipFile(fileobj=raw, filename='', mode='wb', mtime=epoch()) as gz:
        with tarfile.open(fileobj=gz, mode='w') as archive:
            for path in [tree] + sorted(tree.rglob('*')):
                if path.is_symlink():
                    raise ValueError(f'symlink not permitted: {path}')
                info = archive.gettarinfo(str(path), str(path.relative_to(tree.parent)))
                info.uid = info.gid = 0
                info.uname = info.gname = ''
                info.mtime = epoch()
                info.mode = 0o755 if path.is_dir() or os.access(path, os.X_OK) else 0o644
                with path.open('rb') if path.is_file() else io.BytesIO() as stream:
                    archive.addfile(info, stream if path.is_file() else None)


def zip_archive(tree, target):
    import time
    stamp = time.gmtime(max(epoch(), 315532800))[:6]
    with zipfile.ZipFile(target, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(tree.rglob('*')):
            if not path.is_file():
                continue
            info = zipfile.ZipInfo(str(path.relative_to(tree.parent)), stamp)
            info.create_system = 3
            info.external_attr = (0o100644 << 16)
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, path.read_bytes())


def package(platform):
    prerequisites()
    v = version()
    base = fresh(platform) / f'navi8or-{v}'
    base.mkdir()
    for name in DOCS:
        shutil.copyfile(ROOT / name, base / name)
    shutil.copyfile(ROOT / 'scripts/PACKAGE_README.md', base / 'README.md')
    if platform == 'windows':
        # Preserve the actual toolchain notices supplied by Ubuntu/Debian.
        # Alternate distributions can provide their exact equivalents explicitly.
        with (base / 'THIRD_PARTY_NOTICES.md').open('a') as stream:
            stream.write(toolchain_notices())
    (base / 'themes').mkdir()
    for theme in sorted((ROOT / 'themes').glob('*.toml')):
        shutil.copyfile(theme, base / 'themes' / theme.name)
    binary = 'nav' if platform == 'linux' else 'nav.exe'
    shutil.copyfile(OUT / 'bin' / binary, base / binary)
    tool = '' if platform == 'linux' else 'x86_64-w64-mingw32-'
    debug = OUT / 'debug' / f'{binary}.debug'
    debug.parent.mkdir(parents=True, exist_ok=True)
    run(tool + 'objcopy', '--only-keep-debug', str(base / binary), str(debug))
    run(tool + 'strip', '--strip-all', str(base / binary))
    (base / binary).chmod(0o755 if platform == 'linux' else 0o644)
    # Debug files are retained separately, not referenced by a personal path.
    extension = 'tar.gz' if platform == 'linux' else 'zip'
    target = OUT / f'navi8or-{v}-{platform}-x86_64.{extension}'
    (tar_archive if platform == 'linux' else zip_archive)(base, target)
    print(f'{target.relative_to(ROOT)}: {target.stat().st_size} bytes')


def source():
    prerequisites()
    base = fresh('source') / 'navi8or'
    base.mkdir()
    inputs = ('Makefile', 'VERSION', '.gitignore', '.github', 'cmake', 'scripts',
              'include', 'src', 'tests', 'themes', 'third_party', 'docs') + DOCS
    junk = {'.pyc', '.pyo', '.o', '.d', '.a', '.so', '.dll', '.exe', '.swp'}
    for name in inputs:
        path = ROOT / name
        paths = sorted(path.rglob('*')) if path.is_dir() else [path]
        for item in paths:
            if not item.is_file() or '__pycache__' in item.parts or item.suffix in junk or item.name.endswith('~'):
                continue
            target = base / item.relative_to(ROOT)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(item, target)
            target.chmod(0o755 if os.access(item, os.X_OK) else 0o644)
    tar_archive(base, OUT / 'navi8or-source.tar.gz')
    print('Filtered source distribution created')


def toolchain_notices():
    override = os.environ.get('WINDOWS_TOOLCHAIN_NOTICES')
    if override:
        paths = [Path(override)]
    else:
        paths = [Path('/usr/share/doc/mingw-w64-common/copyright'),
                 Path('/usr/share/doc/gcc-mingw-w64-x86-64/copyright')]
    text = '\n## Windows toolchain notices (actual installed packages)\n'
    for path in paths:
        if not path.is_file():
            raise ValueError('missing exact MinGW/GCC notices; set WINDOWS_TOOLCHAIN_NOTICES')
        text += '\n```text\n' + path.read_text() + '\n```\n'
    # Debian-format notices reference these files instead of reproducing them.
    for name in ('GPL-2', 'GPL-3', 'LGPL-2', 'LGPL-2.1'):
        path = Path('/usr/share/common-licenses') / name
        if path.is_file():
            text += f'\n### Referenced full {name} text\n\n```text\n' + path.read_text() + '\n```\n'
    return text


def kit():
    prerequisites()
    v = version()
    base = fresh('relinking') / f'navi8or-{v}-relinking'
    base.mkdir()
    for name in DOCS:
        shutil.copyfile(ROOT / name, base / name)
    (base / 'TOOLCHAIN_NOTICES.md').write_text(toolchain_notices())
    revision = 'b3d560c02fb1268320d2fd1c17fe841b0d93b85f'
    src = ROOT / 'build/linux-deps/src' / f'libsmb2-{revision}'
    # Complete pristine upstream source, including notices and build scripts.
    shutil.copytree(src, base / 'libsmb2')
    shutil.copytree(ROOT / 'third_party/libsmb2-patches', base / 'patches')
    for platform in ('linux', 'windows'):
        tree = base / platform
        objects = tree / 'objects'
        archives = tree / 'lib'
        objects.mkdir(parents=True)
        archives.mkdir()
        compiler = 'cc' if platform == 'linux' else 'x86_64-w64-mingw32-gcc'
        strip = 'strip' if platform == 'linux' else 'x86_64-w64-mingw32-strip'
        inputs = sorted((OUT / 'objects' / platform).rglob('*.o'))
        if not inputs:
            raise ValueError(f'no release objects for {platform}')
        objargs = []
        for item in inputs:
            target = objects / item.relative_to(OUT / 'objects' / platform)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(item, target)
            run(strip, '--strip-debug', str(target))
            objargs.append(str(target.relative_to(tree)))
        names = ['curl', 'ssl', 'crypto', 'z', 'sodium', 'smb2'] if platform == 'linux' else ['curl', 'sodium', 'smb2']
        prefix = ROOT / ('build/linux-deps' if platform == 'linux' else '.deps/windows')
        for name in names:
            target = archives / f'lib{name}.a'
            shutil.copyfile(prefix / 'lib' / target.name, target)
            run(strip, '--strip-debug', str(target))
        args = ['-Wl,--gc-sections']
        if platform == 'windows':
            args += ['-Wl,--stack,8388608', '-Wl,--no-insert-timestamp', '-static-libgcc']
        args += ['-o', 'nav' if platform == 'linux' else 'nav.exe'] + objargs
        if platform == 'windows':
            args += ['-lshell32']
        args += [f'lib/lib{name}.a' for name in names if name != 'smb2']
        command = ' '.join(map(shlex.quote, args)) + ' "$smb2"'
        command += ' -pthread -ldl' if platform == 'linux' else ' -lws2_32 -lcrypt32 -lbcrypt -ladvapi32 -lsecur32 -liphlpapi -lshlwapi -lwldap32 -luser32'
        script = '#!/bin/sh\nset -eu\ncd "$(dirname "$0")"\nsmb2=${1:-lib/libsmb2.a}\ncompiler=${CC:-' + compiler + '}\n"$compiler" ' + command + '\n'
        (tree / 'relink.sh').write_text(script)
        (tree / 'relink.sh').chmod(0o755)
        # Prove the delivered objects/archives can be linked, then discard proof binary.
        run('sh', 'relink.sh', cwd=tree)
        proof = tree / ('nav' if platform == 'linux' else 'nav.exe')
        if platform == 'linux' and run(str(proof), '--version').strip() != f'Navi8or {v}':
            raise ValueError('relinked executable version mismatch')
        proof.unlink()
    shutil.copyfile(ROOT / 'scripts/RELINKING.md', base / 'RELINKING.md')
    tar_archive(base, OUT / f'navi8or-{v}-relinking.tar.gz')
    print('LGPL source + both platform relinkable objects/archives packaged; relinking verified')


def assets():
    v = version()
    return [OUT / f'navi8or-{v}-linux-x86_64.tar.gz',
            OUT / f'navi8or-{v}-windows-x86_64.zip',
            OUT / f'navi8or-{v}-relinking.tar.gz']


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest() if hasattr(hashlib, 'file_digest') else hashlib.sha256(stream.read()).hexdigest()


def checksums():
    (OUT / 'SHA256SUMS').write_text(''.join(f'{digest(p)}  {p.name}\n' for p in assets()))


def linux_check(path):
    if run(str(path), '--version').strip() != f'Navi8or {version()}':
        raise ValueError('Linux --version mismatch')
    description = run('file', str(path))
    if 'ELF 64-bit' not in description or 'x86-64' not in description or 'stripped' not in description or 'not stripped' in description:
        raise ValueError(f'invalid Linux executable: {description}')
    dynamic = run('readelf', '-d', str(path))
    if re.findall(r'Shared library: \[(.*?)\]', dynamic) != ['libc.so.6']:
        raise ValueError('Linux runtime dependencies changed: ' + dynamic)
    versions = re.findall(r'GLIBC_(\d+)\.(\d+)', run('readelf', '--version-info', str(path)))
    maximum = max((int(a), int(b)) for a, b in versions)
    if maximum > (2, 34):
        raise ValueError(f'Linux GLIBC baseline regressed: {maximum}')
    print(description.strip())
    print(run('ldd', str(path)).strip())
    print(f'Maximum GLIBC requirement: {maximum[0]}.{maximum[1]}')


def windows_check(path):
    description = run('file', str(path))
    if 'PE32+' not in description or 'x86-64' not in description:
        raise ValueError('Windows package is not x86-64 PE')
    if re.search(r'\.debug\S*', run('x86_64-w64-mingw32-objdump', '-h', str(path))):
        raise ValueError('Windows executable still contains debug sections')
    imports = re.findall(r'DLL Name: (\S+)', run('x86_64-w64-mingw32-objdump', '-p', str(path)))
    allowed = {'advapi32.dll', 'bcrypt.dll', 'crypt32.dll', 'kernel32.dll', 'msvcrt.dll',
               'secur32.dll', 'shell32.dll', 'user32.dll', 'ws2_32.dll', 'iphlpapi.dll', 'normaliz.dll', 'wldap32.dll'}
    if not imports or set(x.lower() for x in imports) - allowed:
        raise ValueError(f'unexpected Windows DLL imports: {imports}')
    print(description.strip())
    print('Windows system DLLs: ' + ', '.join(imports))


def package_entries(path, binary_package=False):
    if path.suffix == '.zip':
        with zipfile.ZipFile(path) as archive:
            return {i.filename: (archive.read(i), i.external_attr >> 16) for i in archive.infolist() if not i.is_dir()}
    with tarfile.open(path) as archive:
        result = {}
        for item in archive:
            if item.name in result:
                raise ValueError('duplicate archive member')
            if not (item.isfile() or item.isdir()):
                raise ValueError('non-regular archive member')
            if binary_package and item.isdir() and item.name.rstrip('/') not in (f'navi8or-{version()}', f'navi8or-{version()}/themes'):
                raise ValueError('unexpected directory in binary package')
            if item.isfile():
                result[item.name] = (archive.extractfile(item).read(), item.mode)
        return result


def check():
    prerequisites()
    expected_sums = ''.join(f'{digest(p)}  {p.name}\n' for p in assets())
    if (OUT / 'SHA256SUMS').read_text() != expected_sums:
        raise ValueError('SHA256SUMS does not match exact release assets')
    v = version()
    with tempfile.TemporaryDirectory(prefix='navi8or-release-check-') as temp:
        for platform, archive in zip(('linux', 'windows'), assets()[:2]):
            entries = package_entries(archive, binary_package=True)
            expected = {f'navi8or-{v}/{name}' for name in DOCS}
            expected |= {f'navi8or-{v}/themes/{p.name}' for p in (ROOT / 'themes').glob('*.toml')}
            binary = 'nav' if platform == 'linux' else 'nav.exe'
            expected.add(f'navi8or-{v}/{binary}')
            if set(entries) != expected:
                raise ValueError(f'package contents differ: {set(entries) ^ expected}')
            for name, (data, mode) in entries.items():
                if PERSONAL.search(data) or PERSONAL.search(name.encode()):
                    raise ValueError(f'personal development pattern in {name}; not a complete secret scan')
                dest = Path(temp) / platform / name
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(data)
                dest.chmod(mode & 0o777)
            root = Path(temp) / platform / f'navi8or-{v}'
            if platform == 'linux':
                if not os.access(root / binary, os.X_OK):
                    raise ValueError('Linux executable permission missing')
                linux_check(root / binary)
                # This helper uses Navi8or's real profile parser, not a generic TOML parser.
                probe = ROOT / 'build/release-profile-test'
                if not probe.is_file():
                    raise ValueError('run make release-profile-test in the Linux baseline first')
                for profile in (root / 'themes').glob('*.toml'):
                    subprocess.run([str(probe), str(profile)], check=True,
                                   env=dict(os.environ, XDG_CONFIG_HOME=temp))
            else:
                windows_check(root / binary)
        entries = package_entries(assets()[2])
        for name, (data, _) in entries.items():
            # Audited pristine upstream examples use explicitly generic placeholders.
            # Preserve complete library source instead of inventing source changes.
            if name.endswith('/libsmb2/examples/picow/CMakeLists.txt'):
                data = data.replace(b'/home/me/', b'/path/to/')
            if name.endswith('/libsmb2/packaging/RPM/makerpms.sh'):
                data = data.replace(b'/home/mylogin/', b'/path/to/')
            if '..' in Path(name).parts or Path(name).is_absolute() or PERSONAL.search(data):
                raise ValueError(f'unsafe/personal relinking member {name}')
        prefix = f'navi8or-{v}-relinking/'
        for name in ('libsmb2/LICENCE-LGPL-2.1.txt', 'libsmb2/COPYING', 'RELINKING.md',
                     'linux/relink.sh', 'windows/relink.sh', 'TOOLCHAIN_NOTICES.md',
                     'patches/windows-compat.patch'):
            if prefix + name not in entries:
                raise ValueError('missing LGPL relinking input: ' + name)
        for platform in ('linux', 'windows'):
            if not any(name.startswith(prefix + platform + '/objects/') and name.endswith('.o') for name in entries):
                raise ValueError('missing application objects for relinking')
            for library in ('curl', 'sodium', 'smb2'):
                if prefix + platform + f'/lib/lib{library}.a' not in entries:
                    raise ValueError('missing relinking archive')
    print('Release contents, checksums, identities, profiles, ABI/imports and sanitisation: passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('source', 'package', 'kit', 'checksums', 'check', 'version'))
    parser.add_argument('platform', nargs='?', choices=('linux', 'windows'))
    args = parser.parse_args()
    if args.action == 'package':
        if not args.platform:
            parser.error('package requires a platform')
        package(args.platform)
    elif args.action == 'version':
        print(version())
    else:
        globals()[args.action]()
