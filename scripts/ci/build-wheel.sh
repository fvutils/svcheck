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
# Leaves the repaired wheel in dist/.
set -euo pipefail

# The tooling interpreter. The wheel is py3-none-<plat> (it ships an
# executable, not an extension module), so one build serves every Python.
PY=${PY:-/opt/python/cp312-cp312/bin/python}
export PATH="$(dirname "$PY"):$PATH"

# A container checkout is owned by another user, and git refuses it.
git config --global --add safe.directory "$PWD"

echo "=== tools ==="
"$PY" -m pip install -q --upgrade pip ivpm ivpm-build ninja build wheel setuptools

echo "=== dependencies (ivpm, default-dev) ==="
"$PY" -m ivpm update -a -j 1 -d default-dev

echo "=== wheel ==="
rm -rf build dist wheelhouse python/svcheck/bin
"$PY" -m build --wheel --no-isolation

echo "=== auditwheel ==="
# PyPI rejects a plain linux_x86_64 tag; repair retags it manylinux_2_28 after
# checking the executable's symbol versions against that policy.
auditwheel repair -w wheelhouse dist/*.whl
rm dist/*.whl
mv wheelhouse/*.whl dist/
ls -la dist/

echo "=== test against the built wheel ==="
# Against the wheel rather than the source tree: what is tested is what ships.
rm -rf /tmp/svcheck-testenv
"$PY" -m venv /tmp/svcheck-testenv
/tmp/svcheck-testenv/bin/pip install -q pytest dist/*.whl
# From a directory without python/svcheck on the path, so the import is the
# installed package.
/tmp/svcheck-testenv/bin/python -m pytest -q -p no:cacheprovider tests
/tmp/svcheck-testenv/bin/svcheck --version
