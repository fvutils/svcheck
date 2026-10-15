"""Locate and run the native svcheck executable."""
import os
import subprocess
import sys

_EXE = "svcheck.exe" if sys.platform == "win32" else "svcheck"


def native_path() -> str:
    """The native executable: the copy in this package (a wheel or an
    installed source build), or else the CMake build tree of a checkout."""
    pkg_dir = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(pkg_dir, "bin", _EXE),
        os.path.join(pkg_dir, "..", "..", "build", _EXE),
    ]
    for c in candidates:
        if os.path.isfile(c):
            return os.path.normpath(c)
    raise FileNotFoundError(
        "svcheck: native executable not found; build it with "
        "`cmake -S . -B build -G Ninja && cmake --build build` (looked in: %s)"
        % ", ".join(os.path.normpath(c) for c in candidates))


def main(argv=None) -> int:
    args = sys.argv[1:] if argv is None else argv
    return subprocess.call([native_path()] + list(args))
