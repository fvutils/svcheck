# svcheck

SystemVerilog checker: declarative rules (`sv-rules`, YAML + CEL) over slang's model.
C++ core on slang; Python is packaging only.

- Design: `docs/design/svcheck-design.md`. Plan and progress: `docs/design/svcheck-plan.md`.
  Update the plan's item status and its §7 log when an item starts or ends.
- The other `docs/design/*.md` files are the design's companions, copied from pssparser
  (`psstools/pssparser/docs/design/`, untracked there). Edit the copies here.
- pssparser's rule engine is the reference design (`psstools/pssparser/src/lint`). Code may be
  copied from it; never depend on it. Record every copy in the lineage table, design §3.1.

## Build and test

```sh
ivpm update -d default-dev        # deps into packages/ (gitignored)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
uv pip install --python packages/python/bin/python --no-build-isolation -e .   # wheel path; reruns CMake
packages/python/bin/python -m pytest
```

- The CMake configure must stay offline: slang's FetchContent deps are supplied from
  `packages/` (`FETCHCONTENT_SOURCE_DIR_*`, `FETCHCONTENT_FULLY_DISCONNECTED`). A new slang
  FetchContent dep needs an ivpm entry, not network access.
- When bumping slang, bump `fmt` in `ivpm.yaml` to slang's `external/CMakeLists.txt` tag.
- The version lives only in `python/svcheck/__version__.py` (`BASE` + `SUFFIX`); CMake reads it.
  A release is a `v<BASE>` tag; CI stamps `SUFFIX` on branch builds.

## CI

`.github/workflows/ci.yml` and `.forgejo/workflows/ci.yml` are a pair (DVKit convention, the
`dvkit-infra` skill): change them together, and keep the differences to runner constraints. The
build itself is `scripts/ci/build-wheel.sh` (manylinux_2_28 container; trial it with the
`docker run` line in its header). Wheels publish from GitHub on a tag; the release-authority
switch at dvkit.org decides, not the workflow. Never commit internal host names or addresses.
