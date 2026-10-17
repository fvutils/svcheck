#!/usr/bin/env bash
# Build, repair and test the svcheck wheel inside a manylinux_2_28 container.
#
# Shared by .github/workflows/ci.yml and .forgejo/workflows/ci.yml so that the
# two forges build the same way; the workflows add only what differs between
# them (version stamp, dependency URLs, artifact actions). Run it locally with:
#
#   docker run --rm -v "$PWD:/w" -w /w quay.io/pypa/manylinux_2_28_x86_64 \
#     scripts/ci/build-wheel.sh
#
# Leaves the repaired wheels in dist/: one per CPython in PYTHONS (the
# package has extension modules since svcheck-plan P2.6b). CMake builds the
# native code once, with the first; the others reuse build/ (SVCHECK_PREBUILT).
set -euo pipefail

# The tooling interpreter, which builds the first wheel; then the rest.
PY=${PY:-/opt/python/cp312-cp312/bin/python}
PYTHONS=${PYTHONS:-"cp310-cp310 cp311-cp311 cp313-cp313 cp314-cp314"}
export PATH="$(dirname "$PY"):$PATH"

# A container checkout is owned by another user, and git refuses it.
git config --global --add safe.directory "$PWD"

echo "=== tools ==="
"$PY" -m pip install -q --upgrade pip ivpm ivpm-build ninja build wheel setuptools cython
for t in $PYTHONS; do
  /opt/python/$t/bin/python -m pip install -q --upgrade pip ivpm-build ninja build wheel \
    setuptools cython
done

echo "=== dependencies (ivpm, default-dev) ==="
"$PY" -m ivpm update -a -j 1 -d default-dev

echo "=== wheels ==="
rm -rf build dist wheelhouse python/svcheck/bin
"$PY" -m build --wheel --no-isolation
for t in $PYTHONS; do
  SVCHECK_PREBUILT=1 /opt/python/$t/bin/python -m build --wheel --no-isolation
done

echo "=== auditwheel ==="
# PyPI rejects a plain linux_x86_64 tag; repair retags each manylinux_2_28
# after checking the native code's symbol versions against that policy.
for w in dist/*.whl; do
  auditwheel repair -w wheelhouse "$w"
done
rm dist/*.whl
mv wheelhouse/*.whl dist/
ls -la dist/

echo "=== test against the built wheels ==="
# Against the wheel rather than the source tree: what is tested is what ships.
rm -rf /tmp/svcheck-testenv
"$PY" -m venv /tmp/svcheck-testenv
/tmp/svcheck-testenv/bin/pip install -q --no-index --find-links dist svcheck
/tmp/svcheck-testenv/bin/pip install -q pytest cel-python
# From a directory without python/svcheck on the path, so the import is the
# installed package.
/tmp/svcheck-testenv/bin/python -m pytest -q -p no:cacheprovider tests
/tmp/svcheck-testenv/bin/svcheck --version

echo "=== the other CPythons' wheels: install and import ==="
for t in $PYTHONS; do
  rm -rf /tmp/svcheck-smoke
  /opt/python/$t/bin/python -m venv /tmp/svcheck-smoke
  /tmp/svcheck-smoke/bin/pip install -q --no-index --find-links dist svcheck
  (cd /tmp && /tmp/svcheck-smoke/bin/python -c '
import svcheck, sys
open("/tmp/svcheck-smoke/t.sv", "w").write("package p; class c; endclass endpackage\n")
c = svcheck.compile(["/tmp/svcheck-smoke/t.sv"])
assert "p" in [p.getName() for p in c.design.packages()], "no package p"
assert svcheck.run_cli(["--version"])[0] == 0
print(sys.version.split()[0], "ok")
')
done
