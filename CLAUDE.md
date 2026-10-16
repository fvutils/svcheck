# svcheck

SystemVerilog checker: declarative rules (`sv-rules`, YAML + CEL) over slang's model.
C++ core on slang; Python is packaging only.

- Design: `docs/design/svcheck-design.md`. Plan and progress: `docs/design/svcheck-plan.md`.
  Update the plan's item status and its §7 log when an item starts or ends.
- `docs/design/` is local only (gitignored, never commit it), as is `lowrisc-style-guides/`.
  The other `docs/design/*.md` files are the design's companions, copied from pssparser
  (`psstools/pssparser/docs/design/`, untracked there). Edit the copies here.
- pssparser's rule engine is the reference design (`psstools/pssparser/src/lint`). Code may be
  copied from it; never depend on it. Record every copy in the lineage table, design §3.1.

## Engine

`src/engine/` is the fork of pssparser's engine (P1.1), built as `svcheck_engine`. It must not
include slang or SV types: the host gives it a schema (`schema/Reflect.h`, `install()`), a
`run::Context` and a function library (`cel::setHostLib`). Its tests (`tests/engine/`) drive the
shipped binary through `svcheck --engine-test-hooks` (`tests/engine/hooks.py`); the node hooks
take SV source text, which the host parses alone as `snippet.sv`. Copied tests the host cannot
serve yet are listed, with the reason, in `tests/engine/needs_schema.txt`. Remove a line from
that file when its test runs; never add one to hide a failure.

`src/host/` is the SV host (`svcheck_host`, on slang). The syntax schema's tables are generated
at build time from slang's `scripts/syntax.txt` by `scripts/gen_syntax_schema.py` (its docstring
gives the layout); `Syntax.{h,cpp}` holds the hand-written half: tokens, locations, install.

The semantic layer (P1.3) is the curated model (`src/ast/*.yaml`, pyastbuilder, generated at
configure time by `scripts/gen_sem_model.py`; lowered from slang by `src/lower`), installed
before the syntax schema in one merged table (`Schema.cpp`); `--semantic=none` leaves it out.
Q-SC5 compared it with an accessor table over slang's AST, which P1.8 deleted
(`docs/design/svcheck-spike-report.md`). `svcheck --engine-test-hooks --semantic=model
--uvm=packages/uvm-core/src` compiles the hooks' source; `tests/host/test_semantic.py` runs
`tests/host/rules/` on it.

P1.4 added, all in the engine unless named:
- the examples harness (`run/Examples.h`; hook `lintExamples`, `check_examples()` in
  `tests/engine/hooks.py`), with the host compiling each example (`hostCompiler`,
  `src/host/HookParser.cpp`);
- `uses:` and file-level options (the loader; files read through `LintEngine::setReader`);
- contexts: `contexts:` classifiers, run by `run/Classify.cpp` before the rules. The host's
  half is `src/host/SvContext.cpp` (units, edges, `context(n)`).

The library functions over the model (`src/host/SvLib.cpp`) are registered only when the model
is the layer installed.

P1.5 added the lint CLI: `svcheck --rules r.yaml -f tb.f [--format text|json|sarif]
[--contexts] [--library-dir DIR]` (`src/cli/Lint.cpp`; the host's compilation, library code and
rule scopes in `src/host/Lint.cpp`). `applies_to` on rules is applied by the runner from the
labels the classify stage leaves on `run::Context`. The engine hooks hold whole engines by id
(`factory.mkLintEngine()` in `tests/engine/conftest.py`).

P1.6 added the spike's 10 rules, in `rules/spike/` (provisional IDs; `uvm.yaml` reads
`uvm-vocab.yaml` through `uses:`), tested by `tests/host/test_spike_rules.py` (examples, predicate
length, a Verible fixture). `scripts/verible_equiv.py -- <svcheck file args>` diffs the
Verible-equivalent rules against Verible (`packages/verible`, from ivpm), and
`scripts/mk_filelist.py` writes a `-f` list for a source tree that has none. The lint JSON lists
the compilation's user files (`compilation.files`). Findings in a file with a syntax error, or in
library code, are not shown; this works per file, `--single-unit` included.

P1.6b ran the contexts trial: `rules/trial/` (three CTX rules; `contexts.yaml` uses the content
classifier set, `classify-convention.yaml` adds the weak one as a second `--rules`), run by
`scripts/ctx_trial.py` on `tests/corpora/*.f`. Those lists name local snapshots in `packages/`
(not ivpm deps; how to rebuild them, and their fixes, in `tests/corpora/README.md`). Results:
`docs/design/sv-contexts-trial.md`. The model has `Process` (procedural blocks), and the library
`subclasses(c)`.

P1.7 measured the corpora: `svcheck --stats` (phase times and heap; `stats` in the JSON), and
`scripts/measure.py` prints the plan's §6.1 tables. `tests/corpora/fwvip.f` is the VIPs with no
bench (S-1/S-2); `ctx-positive.f` is a positive corpus for the trial rules, with its sources in
the repo. UVM 1.2 is an ivpm dependency (`packages/uvm-1.2/src`); the examples run on both UVM
versions. `scripts/wasm_size.sh TOOLS BUILD` builds svcheck for WASI (it downloads wasi-sdk,
binaryen and wasmtime into TOOLS) and prints its size. A generic class's model body comes from a
specialisation the design makes, and the default only when there is none.

## Build and test

```sh
ivpm update -d default-dev        # deps into packages/ (gitignored)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
uv pip install --python packages/python/bin/python --no-build-isolation -e .   # wheel path; reruns CMake
packages/python/bin/python -m pytest
```

The tests run `python/svcheck/bin/svcheck`, which only the `uv pip install` step updates: rerun
it after a build, or the tests see the old binary. The wheel build runs the venv's `ninja`
(`packages/python/bin`), while `cmake --build` runs the one in `build/CMakeCache.txt`
(`/usr/bin/ninja`); the two read each other's logs as changed commands, so each switch rebuilds
everything. Build with `packages/python/bin/ninja -C build` instead.

- The CMake configure must stay offline: slang's FetchContent deps are supplied from
  `packages/` (`FETCHCONTENT_SOURCE_DIR_*`, `FETCHCONTENT_FULLY_DISCONNECTED`). A new slang
  FetchContent dep needs an ivpm entry, not network access.
- When bumping slang, bump `fmt` in `ivpm.yaml` to slang's `external/CMakeLists.txt` tag, and
  re-check the local patches (`patches/slang/README.md`, "Bumping slang").
- slang issues are tracked locally, not upstream: each in `patches/slang/` with a patch that ivpm
  applies to the slang archive (`patches:` on both `slang` entries in `ivpm.yaml`) and a repro
  that `tests/host/test_slang_patches.py` compiles. After `ivpm update` changes slang's sources,
  rebuild. Instance caching stays on; the patches make it safe.
- The version lives only in `python/svcheck/__version__.py` (`BASE` + `SUFFIX`); CMake reads it.
  A release is a `v<BASE>` tag; CI stamps `SUFFIX` on branch builds.

## CI

`.github/workflows/ci.yml` and `.forgejo/workflows/ci.yml` are a pair (DVKit convention, the
`dvkit-infra` skill): change them together, and keep the differences to runner constraints. The
build itself is `scripts/ci/build-wheel.sh` (manylinux_2_28 container; trial it with the
`docker run` line in its header). Wheels publish from GitHub on a tag; the release-authority
switch at dvkit.org decides, not the workflow. Never commit internal host names or addresses.
