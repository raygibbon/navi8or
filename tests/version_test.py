#!/usr/bin/env python3
"""Single source, deterministic generation, validation and shared build rules."""
from pathlib import Path
import os
import subprocess
import tempfile

project = Path(__file__).resolve().parent.parent
version = (project / "VERSION").read_text().strip()
with tempfile.TemporaryDirectory(prefix="nav-version-") as temporary:
    root = Path(temporary); source, header = root / "VERSION", root / "generated" / "nav_version.h"
    def generate(success=True):
        result = subprocess.run(["sh", str(project / "scripts/generate-version.sh"), str(source), str(header)],
                                capture_output=True)
        assert (result.returncode == 0) == success, result.stderr
    source.write_text(version + "\n"); generate()
    original = header.read_bytes(); timestamp = header.stat().st_mtime_ns
    assert f'#define NAV_VERSION "{version}"'.encode() in original
    generate(); assert header.read_bytes() == original and header.stat().st_mtime_ns == timestamp
    parts = version.split("."); parts[2] = str(int(parts[2]) + 1); next_version = ".".join(parts)
    source.write_text(next_version + "\n"); generate()
    assert f'#define NAV_VERSION "{next_version}"' in header.read_text()
    current = header.read_bytes()
    for invalid in ("", "not-a-version\n", version + "\nextra\n", version + '"\n', version + " \n"):
        source.write_text(invalid); generate(False); assert header.read_bytes() == current
    assert not list(header.parent.glob("*.tmp.*"))
    # Both top-level build branches invoke the same generator/source/header.
    for target in ("native", "windows"):
        subprocess.run(["make", "-C", str(project), f"TARGET={target}", "version-header"], check=True,
                       env=dict(os.environ, USE_LINUX_DEPS="0"), stdout=subprocess.DEVNULL)
        generated = project / "build/generated/nav_version.h"
        assert generated.read_bytes() == original
print("Version generation: canonical source, deterministic/unchanged timestamp, updates, invalid input and native/MinGW parity: passed")
