"""P0 smoke tests: the native executable runs and compiles UVM."""
import os
import subprocess

import pytest

from svcheck import native_path

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
UVM_F = os.path.join(ROOT, "tests", "data", "uvm-2020.F")


def run(*args):
    return subprocess.run([native_path(), *args], capture_output=True, text=True)


def test_version():
    r = run("--version")
    assert r.returncode == 0
    assert r.stdout.startswith("svcheck ")
    assert "slang 11.0" in r.stdout


@pytest.mark.skipif(not os.path.isdir(os.path.join(ROOT, "packages", "uvm-core")),
                    reason="UVM not fetched (ivpm update -d default-dev)")
def test_uvm_pkg_compiles_without_errors():
    r = run("--dump-diags", "-F", UVM_F)
    assert r.returncode == 0, r.stdout + r.stderr
    assert "(0 errors," in r.stdout
