"""svcheck's local slang patches (patches/slang/README.md): each issue's repro,
compiled by the shipped binary with slang's defaults (instance caching on).
A failure here after a slang bump means a patch no longer applies or no
longer fixes its issue.
"""
import os
import subprocess

import pytest

from svcheck import native_path

HERE = os.path.dirname(os.path.abspath(__file__))
REPROS = os.path.join(HERE, "slang")


@pytest.mark.parametrize("repro, args", [
    # SL-1: false "cyclic dependency" on a cached duplicate instance's interface.
    ("sl1-param-cycle.sv", ["--top", "top"]),
])
def test_repro_compiles(repro, args):
    r = subprocess.run([native_path(), os.path.join(REPROS, repro)] + args,
                       capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, r.stderr
    assert "error" not in r.stderr, r.stderr
