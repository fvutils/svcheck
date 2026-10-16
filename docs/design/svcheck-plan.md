# svcheck: phased plan

| | |
|---|---|
| **Status** | 2026-10-15: P0 done except P0.5's Forgejo run, which isn't confirmed yet (GitHub is green). **P1.1 done** (`5aae887`), **P1.2 done** (not committed yet); P1.3 next. R-1…R-5 approved. |
| **Implements** | `svcheck-design.md` (the design). This plan refines its §10 into items that can be tracked; it does not change the design's decisions. Where it recommends something new, the item says so and lists it under §2. |
| **Tracking** | Each item has an ID (`P1.3`), a status and an exit criterion. Update the status column and add a dated line to the log (§7) when an item starts or ends. |

Status values: `todo`, `active`, `done`, `blocked (<why>)`, `dropped (<why>)`.

## 0. Summary

| Phase | = design §10 | What | Rough size | Gate at the end |
|---|---|---|---|---|
| **P0** Bootstrap | (new) | companion docs moved in; repo skeleton; slang, rapidyaml, pyastbuilder and UVM through ivpm; a native binary that compiles `uvm_pkg`; wheel and CI | ~1 week | builds from clean on CI |
| **P1** Spike | SC1 | forked engine with a host interface; slang syntax schema; curated model for the spike subset; 10 rules; corpora run and measured | 3–4 weeks | **go/no-go**, Q-SC5 decided |
| **P2** First release | SC2 | the rest of the pipeline wired to SV; `uvm.yaml` + 36-rule consensus pack + presets; level 1 elaboration in C++; config; precision and performance gates | 6–8 weeks | starter pack meets the precision bar; release 0.1 on PyPI |
| **P3** Level 2 | SC3 | run-0 interpreter, intrinsics, simulator-dump oracle, level-2 rules | 6–10 weeks | facet diff clean on the corpora |
| **P4** Cross-model | SC4 | vif → RTL, HDL paths, Tier B macro rules, Verible pack if asked | planned at P3's end | as P3 |
| **P5** Reach | SC5 | WASM/editor, MCP check tool, agent measurement | planned at P4's end | measured, design §9 |

Sizes are calendar estimates for one developer with agent help. They exist to make slippage
visible, not as commitments. P4 and P5 are listed so the order is visible. They are planned
in detail only when P3 ends.

## 1. What this plan learned from the code (2026-10-15)

These facts were checked against the repositories and shape the plan:

| # | Fact | Effect |
|---|---|---|
| L-1 | pssparser's engine (`psstools/pssparser/src/lint`, rev `57c6b14`) is complete through C8: about 21k lines. | Since Q-SC1 allows literal copies, P1 **forks** the engine instead of writing the "minimal engine" design §10 describes (R-1). |
| L-2 | PSS coupling is local. `cel/` (6.8k lines), `regex/`, `yaml/`, `util/`, `catalog/` and `rules/` include no PSS headers. `run/` (3 of 4 files), `lib/` (2 of 4), `pipeline/Waivers.cpp`, `LintEngine.*` and `LintTestHooks.*` do. | The fork does C9.1, C9.3 and C9.4 on svcheck's side: the cut points are already listed in lint-core-plan C9. |
| L-3 | The companion documents (`sv-lint-survey.md`, `sv-lint-demand.md`, `sv-uvm-rules-assessment.md`, `sv-uvm-rules-research/`, `sv-uvm-elaboration-checks.md`, `model-query-database.md`) are still only in `pssparser/docs/design/`. `svcheck-design.md` is in both repositories, identical. pssparser's `docs/design/` is untracked in git. | Design §13 is P0's first item. svcheck becomes the only committed home; nothing changes in pssparser. |
| L-4 | sphinx-systemverilog's `model/uvm.py` is 3,258 lines of Python on pyslang (≥ 7.0). | Level 1 port is sized at the low end of a few weeks; its fixtures become the P2 oracle. |
| L-5 | No slang checkout exists locally; the design's measurements used pyslang 11.0.0. | P0 pins slang **v11.0** so native and pyslang numbers are comparable. |
| L-6 | `uvm-1.2` is in `fvutils`; the design's measurements and examples assume Accellera 1800.2-2020. | P0 pulls 1800.2-2020.3.1 through ivpm as the default; 1.2 is a second test configuration (S-7). |
| L-7 | Corpora present locally: `uvm-tb-proto/out`, `featherweight-ip/fw-wb-dma/tests/uvm`. OpenTitan and CORE-V-VERIF are not. | P1 runs on the two local corpora plus one OpenTitan IP; the full corpora join in P2. |

## 2. Recommendations that go beyond the design

Each is a deviation from or an addition to the design. **All five approved by the user, 2026-10-15**; R-5 with the prefix `SVCHK` rather than `SVC`.

| ID | Recommendation | Why | If rejected |
|---|---|---|---|
| **R-1** | **Fork the whole PSS-free engine in P1** (cel, regex, yaml, util, catalog, rules, run, pipeline), not a minimal one. Record each directory in the lineage table (design §3.1). | It is tested code from C0–C8, with the C0 findings (F-1…F-10) already fixed. Writing a minimal one would repeat that work. It also moves the CEL type checker, selection and SARIF from P2 into P1 nearly for free. | P1 grows by ~2 weeks and P2 keeps those items. |
| **R-2** | Fork **one revision**, all directories at once, and do not pull later pssparser fixes by default. A fix that matters is copied by hand and logged in the lineage table's "Changed since" column. | One base revision makes a later comparison of the two engines simple. | Per-directory revisions; more bookkeeping. |
| **R-3** | **Q-SC4: port level 1 to C++ in P2**, and use sphinx-systemverilog's JSON as the *oracle* rather than a runtime input. | The wheel should not depend on pyslang. Level 1 must also run in WASM later. The oracle keeps the port honest. | A JSON facet loader ships in P2. The port moves to P3. |
| **R-4** | **Q-SC3: `.f` files only in P1–P2** (with `+incdir`, `+define`, `-f` nesting). FuseSoC and `flow.yaml` come in P2.9 if a corpus needs them. | `.f` files are universal and every corpus has them or can have one generated. | Add a P2 item per format. |
| **R-5** | **Q-SC2: settle the namespace in P0** with a provisional prefix (**`SVCHK`**, chosen over `SVC`, which reads as "service"), so rule IDs written in P1 need no renaming. | IDs appear in examples, waivers and docs from the first rule. | P1 rules use placeholder IDs. They are renamed at P2.0. |

## 3. Decision gates

When each open question must be answered, so that work never waits on one by surprise:

| Question | Needed by | Who | Proposed answer |
|---|---|---|---|
| R-1 … R-5 | P1 start | user | **approved 2026-10-15** |
| Q-SC2 namespace | P0.6 | user | **provisional `SVCHK`, 2026-10-15**; final at P2.0 |
| Q-SC3 project files | P1.5 | user | **R-4, approved 2026-10-15** |
| Q-UVM2 `MacroUse` kind vs function | P1.4 | decided in P1 by trying both on S-3 rules | — |
| **Q-SC5** curated model vs accessor table | **P1 exit** | measured (design §3.6 criterion) | — |
| Q-SC4 level 1 port | P2 start | user | **R-3, approved 2026-10-15** |
| Q-UVM1 roles as YAML helpers | P2.3 | user | yes (design recommends) |
| Q-UVM3 presets ship with the pack | P2.5 | user | — |
| Q-UVM4 slang warnings namespace | P2.2 | user | — |
| Q-EL3 default roots, Q-EL4 partial-in-tests | P3 start | user | — |
| Q-EL5 whether the dump producer ships | P3.6 | user | — |

## 4. Phases in detail

### P0. Bootstrap (~1 week)

| ID | Item | Exit | Status |
|---|---|---|---|
| P0.1 | **Bring in the design record (design §13).** Copy the companions in L-3 into `docs/design/` and commit them here. Update the design's *Home* line and fix the relative links. pssparser is not touched: its `docs/design/` is untracked and is not committed there (user, 2026-10-15). | all companion links in `svcheck-design.md` resolve locally | done |
| P0.2 | **Repository skeleton** to design §8: `CMakeLists.txt`, `ivpm.yaml`, `pyproject.toml`, `src/{ast,lower,host,elab,cli}`, `python/svcheck`, `rules/`, `scripts/`, `tests/`. Also a `CLAUDE.md` with build and test commands. README replaced with a one-paragraph description. | `ivpm update && cmake --build` from clean | done |
| P0.3 | **Dependencies through ivpm:** slang v11.0 (pinned tag), rapidyaml v0.16.0 (pssparser's pin), pyastbuilder, UVM 1800.2-2020.3.1 (1.2 deferred to P1.7). Settle slang's own fetched dependencies (fmt, boost-unordered, mimalloc). Either vendor them through ivpm or turn them off (`SLANG_USE_MIMALLOC=OFF`), so the build has no network access at CMake time. | clean build offline after `ivpm update` | done (P0-2) |
| P0.4 | **Hello slang:** `svcheck --dump-diags -f uvm.f` compiles `uvm_pkg` and prints slang's diagnostic count and timing. | 0.12 s ± noise vs. pyslang's number (design §6) | done: 0.08 s (§6) |
| P0.5 | **Wheel and CI.** Use the same ivpm-build/cython path as pssparser, with the CLI as a console script. CI runs build plus pytest. Set up with the `dvkit-infra` skill: wheels go to PyPI from **GitHub** (release authority `github`, the DVKit default); docs publish from **Forgejo** to dvkit.org (a `docs.yml` once there are docs to build). | green CI on a trivial test | GitHub green (run 38813715993, `dfd92a3`). Forgejo not confirmed: the repo is private, so its Actions setting and runs can't be seen from here (P0-7) |
| P0.6 | Q-SC2 settled (R-5); recorded in design §2. | decision logged | done: provisional prefix `SVCHK` |

### P1. Spike = SC1 (3–4 weeks)

Order: P1.1 and P1.2 in parallel. P1.3 needs P1.2's node handles. P1.4–P1.6 need all three.

| ID | Item | Exit | Status |
|---|---|---|---|
| P1.1 | **Fork the engine** (R-1, R-2). Copy `cel/ regex/ yaml/ util/ catalog/ rules/ run/ pipeline/` from pssparser `57c6b14` into `src/engine/`. Add a lineage-table row per directory. Then cut the PSS out: `run/` walks a host `INodeHandle` (C9.1); the function registry is filled by the host (C9.2); waiver *extraction* moves to the host (C9.3); `LintEngine` takes a host for the schema, root key, snippet compile and ID policy (C9.4). The engine is a CMake target with no slang include path (C9.5). Copy the engine's CEL conformance and regex tests along with it. | engine target builds alone; copied CEL/regex/YAML tests pass; root key `sv-rules:` accepted | **done** (P1-1…P1-5): 1033 pass. Of the copied tests, 203 that need a PSS schema or PSS sources are held back for P1.2, and 4 for P1.5 (`tests/engine/needs_schema.txt`) |
| P1.2 | **Syntax schema and handles.** `scripts/gen_syntax_schema.py` reads slang's `scripts/syntax.txt` and emits the kind table and field accessors; `kindmap` groups become `kind: [...]` lists. Node handles over `SyntaxNode` carry kind, fields, children, parent, identity and a macro-aware location (S-3). | a CEL rule matching `ClassDeclaration` fires on a snippet; `tests/engine/needs_schema.txt` empty (its tests re-targeted at SV kinds and snippets) | **done** (P1-6…P1-10): the rule fires (`tests/host/`). All but 7 tests run: 4 wait for P1.5 as before, and 3 for a flags enum, which the SV syntax schema doesn't have (P1-9) |
| P1.3 | **Curated semantic model, spike subset** (design §3.6). In pyastbuilder YAML: package, class (base, params, specialisation, `isAbstract`), subroutine (virtual, override), call, assignment, case, `Opaque`. Write the lowering pass from slang's semantic model: user code in full, library code as declarations only, each node with a back-handle to its slang symbol. **For Q-SC5**, also write a thin accessor table over slang's `ClassType`/`SubroutineSymbol` that is just enough for the 4 UVM rules, so both sides can be compared on the same rules. | both paths answer the 4 UVM rules' examples | todo |
| P1.4 | **Function library, spike subset:** navigation; `derives_from`, `member`, `members`, `overrides`, `is_super_call`, `in_library`, `resolves_to`, `type_of`, `const_value`, `macro_of` (decides Q-UVM2); `uses:` and file-level options (divergence-log rows). Snippet compilation for `examples:` against a cached `uvm_pkg`. | the 10 rules' examples run through the copied examples-as-tests harness | todo |
| P1.5 | **CLI and compilation context:** `.f` reader (R-4), `--rules`, `--format text|json|sarif` (already in the fork), scope computation, and **not run** reporting (design §3.3). | `svcheck -f tb.f --rules r.yaml` on fw-wb-dma | todo |
| P1.6 | **The 10 rules.** 3 Verible-equivalent (sv-lint-survey §3), UVM001/010/020/030 (assessment §4), and 3 of FWH001–005 (sv-lint-survey). Plus `scripts/verible_equiv.py`, which runs Verible and svcheck on the same files and diffs the findings. | every predicate ≤ 15 lines; Verible diff empty or explained | todo |
| P1.7 | **Corpora and measurements:** uvm-tb-proto, fw-wb-dma, one OpenTitan IP `dv/`. Measure compile, lowering (time and memory against compile+bind), and rule time. Document how S-1 and S-2 behave, on `fwvip-*` with no test, including P0-1 (slang's per-specialisation diagnostics vary between runs). Add UVM 1.2 as a second configuration. Note the WASM size, without a gate. | numbers in §6 of this plan | todo |
| P1.8 | **Spike report and go/no-go:** `docs/design/svcheck-spike-report.md`, with the measurements, Q-SC5's answer against the 20% / rule-length criterion, Q-UVM2's answer, S-1/S-2 findings, and the next steps. If the accessor table wins, delete the losing path's code. | user says go | todo |

### P2. First release = SC2 (6–8 weeks)

Planned at the level below. Items are refined at P1's exit with what the spike learned.

| ID | Item | Exit | Status |
|---|---|---|---|
| P2.0 | Re-plan from the spike report; final namespace; rename any placeholder IDs | this table updated | todo |
| P2.1 | Semantic model (or accessor table) extended to everything the 36 rules need, per family; generated Sphinx node reference for rule authors | node reference builds | todo |
| P2.2 | Pipeline on SV: profiles, per-path overrides, **SV-comment waivers**, `--explain`, `--check-rules`, catalogue; slang's own warnings mapped under their own namespace (Q-UVM4) | CLI option parity with pssparser's table | todo |
| P2.3 | `rules/uvm.yaml` vocabulary: roles, phase names, `factory_create`, config_db helpers | used by every pack rule | todo |
| P2.4 | **Consensus pack, 36 rules**, in family batches: F, K, T, S, P, G, A, L. Each rule has a firing example and a near-miss | examples harness green per batch | todo |
| P2.5 | Presets: `uvm-consensus`, `easier-uvm`, `lowrisc-dv`, `core-v`, as options on rules (assessment §5.2) | each preset runs on its corpus | todo |
| P2.6 | **Level 1 elaboration in C++** (R-3): creation sites, `connect`, config_db, overrides, vif bindings as facets; facet inference in the loader. Oracle: sphinx-systemverilog's fixtures diffed as JSON | oracle diff clean on its fixtures | todo |
| P2.7 | Level-1 facet rules: orphan set/get, broken override, unread or undriven vif, non-`this` parent | examples + corpora | todo |
| P2.8 | `svcheck.toml` / `[svcheck]` config | documented | todo |
| P2.9 | Full corpora via ivpm (OpenTitan `hw/ip/*/dv`, CORE-V-VERIF); precision harness (lint-criteria); performance gate (as C8) | gates in CI | todo |
| P2.10 | **Release 0.1:** register the `svcheck` PyPI name with a real release; docs site; CHANGELOG | wheel installs and runs on a clean machine | todo |

### P3. Level 2 interpreter = SC3 (6–10 weeks)

Outline only. Detailed at P2's exit.

| ID | Item |
|---|---|
| P3.1 | `uvm_facet_dumper` + Verilator UVM flow: the simulator-dump oracle, built *first* |
| P3.2 | Concrete interpreter: one test, fixed plusargs; `initial` → `run_test` → build/connect/EOE |
| P3.3 | Intrinsics: factory, config_db with per-version precedence, naming, `connect`, objections, `start`, `uvm_reg*` |
| P3.4 | Unknowns: `may` results, `*` multiplicity, havoc points, completeness tests |
| P3.5 | All tests as roots; de-duplication by location; facet cache with read sets |
| P3.6 | Level-2 rules (design §3.4 list) to the precision bar |

### P4, P5

As design §10, SC4 and SC5. Not detailed here.

## 5. Risks to the plan (in addition to design §7)

| ID | Risk | Mitigation |
|---|---|---|
| PR-1 | slang's CMake fetches fmt/boost/mimalloc at configure time, which conflicts with ivpm's offline model | **closed 2026-10-15** (P0-2) |
| PR-2 | The engine fork's PSS cuts (C9.1–C9.4) touch more than L-2 suggests: `LintTestHooks` is 1.3k lines | timebox P1.1 to 1 week. If it overruns, wrap PSS-only code in `#if 0` and carry on |
| PR-3 | pyastbuilder AB-1..AB-3, in particular AB-2 (Python construction) | P1.3 needs only C++ construction. Python bindings are needed only from P2.1 on |
| PR-4 | Q-SC5 comes out marginal (about 20%) | the spike report gives both numbers and a recommendation. The user decides, and the losing path is deleted at once |
| PR-5 | Corpora that need tool-specific `.f` contents (vendor `+define`s, Questa-only code) | a per-corpus `.f` in `tests/corpora/`, kept by svcheck |

## 6. Measurements

Filled in by P0.4 and P1.7.

| What | Corpus | Value | Date |
|---|---|---|---|
| Parse, native (slang 11.0, Release, no mimalloc) | UVM 2020.3.1 `uvm_pkg.sv` | 0.033 s | 2026-10-15 |
| Elaborate + all diagnostics, native | same | 0.048 s | 2026-10-15 |
| Total, native (pyslang 11.0.0: 0.12 s) | same | 0.08 s | 2026-10-15 |
| slang diagnostics, raw (all enabled-or-not warnings, 0 errors) | same | 1093–1099, varies per run (P0-1) | 2026-10-15 |
| Clean native build (slang + svcheck, 140 steps) | — | 53 s wall on this workstation | 2026-10-15 |
| Wheel size (`py3-none-linux_x86_64`, executable only) | — | 9.8 MB uncompressed | 2026-10-15 |

### P0 findings

| # | Finding | Effect |
|---|---|---|
| P0-1 | slang's raw diagnostic list for `uvm_pkg` is **not deterministic**: only `DynamicCastConst` varies (35–43 per run, also with `-j 1`). It is reported per class specialisation, and how many survive slang's de-duplication changes between runs | an S-2 input for P1.7. svcheck must not expose slang's raw list as findings unordered or un-deduplicated. `--dump-diags` prints a per-code histogram for this |
| P0-2 | slang v11 FetchContent-declares fmt 12.1.0 and mimalloc. fmt is supplied from ivpm through `FETCHCONTENT_SOURCE_DIR_FMT`, and `FETCHCONTENT_FULLY_DISCONNECTED` is on. mimalloc is off. boost-unordered is vendored in slang. Verified: no fetch sub-build exists in the build tree | PR-1 closed. Re-enabling mimalloc (P2.9) needs the same treatment as fmt |
| P0-3 | slang's version script, built from a release archive, picks up svcheck's own `.git` hash: it checks the `.git` against the *top* source directory | cosmetic. `--version` prints slang's major.minor.patch only |
| P0-4 | ivpm-build's CMake hook runs only for extension modules. The wheel therefore uses a `build_py` hook (`CmakeBuilder`), copies the executable to `svcheck/bin/`, and is tagged `py3-none-<plat>`. It installs and runs in a clean Python 3.14 venv. It is a `linux_x86_64` tag, not manylinux: that is release CI's job (P2.10) | revisit when Cython bindings arrive (P2) |
| P0-5 | slang's `Driver` already reads `-f`/`-F`, `+incdir+` and `+define+` | R-4 needs little code. P1.5 adds only the compilation-context checks and **not run** reporting |
| P0-6 | ivpm unpacks rapidyaml with its top directory (`packages/rapidyaml/rapidyaml.v0.16.0/`), as pssparser's F-1 found. slang, fmt and uvm-core are unpacked flat | the P1.1 engine build globs for the rapidyaml tree, as `lint.cmake` does |
| P0-7 | The wheel is built in a manylinux_2_28 container and repaired by auditwheel to `py3-none-manylinux_2_27_x86_64.manylinux_2_28_x86_64` (3.3 MB). The workflow pair shares one build script; Forgejo uses pssparser's CI image (manylinux plus node, which act needs) and maps git deps to git.dvkit.org. The release archives (slang, fmt, rapidyaml, uvm-core) still come from github.com on both forges. The version file is now `BASE` + `SUFFIX`, so CI can stamp dev builds | a Forgejo build still depends on GitHub for the archives. Mirror them if that matters. Linux x86_64 only for now; aarch64/macOS/Windows wheels are a P2.10 question |

### P1 findings

| # | Finding | Effect |
|---|---|---|
| P1-1 | The schema cut is smaller than C9.1 suggested. pssparser's engine reads its schema only through `Reflect.h`'s tables (class, field, enum and struct arrays with accessor function pointers), so making those runtime variables the host installs (`src/engine/schema/Reflect.h`) leaves `cel/`, `rules/` and `run/Walker` source almost unchanged. There is no `INodeHandle` class: a node is `{kind, ptr}`, and the host's `FieldInfo` accessors interpret `ptr` | P1.2 writes slang's syntax tables in gen-reflect's layout. The Walker's PSS conventions (a `location` struct field, `name: ExprId`, the implicit `FieldCompRef`) have no effect without those classes; P1.2 replaces them with the SV host's equivalents |
| P1-2 | The root key and ID policy (C9.4) are literals in the fork (`sv-rules`, `SVCHK`), not a host object: svcheck has one host. `SVCHK` is the reserved namespace and the built-in one; the engine's markers keep pssparser's numbers (SVCHK000, 004, 007, 201–206, 210–223, 230) | built-in rule numbers must avoid those until P2.0 settles the namespace (design §4) |
| P1-3 | Snippet compilation for `examples:` is not in pssparser's C++ engine: its examples-as-tests harness is Python over a PSS parse. Nothing was cut for it in P1.1 | it lands with the harness in P1.4, as a host hook |
| P1-4 | The tests drive the shipped binary: `svcheck --engine-test-hooks` serves `LintTestHooks::dispatch` as JSON lines, and `tests/engine/hooks.py` has the method names of pssparser's Cython hooks, so the copied tests run with only paths and names changed. The CEL differential test (2000 expressions against cel-python) passes; cel-python is now in `default-dev` and in the CI test venv | the Python bindings stay a P2 question (P0-4, PR-3) |
| P1-5 | The tests held back: 85 parse PSS source, 76 name PSS schema types, 42 match PSS node kinds, 4 drive a whole `LintEngine` (`mkLintEngine`). I checked each failure's message before listing it: none was a fault in the fork. The namespace policy is checked separately (`test_fork_policy.py`) | P1.2 re-targets the first three groups; P1.5 adds engine-level hooks for the last |
| P1-6 | The syntax schema has two kinds of class: one per slang syntax type, named as the C++ type (`BinaryExpressionSyntax`, with the fields), and one per `SyntaxKind` (`AddExpression`, deriving from its type). That is 968 classes and 1,332 fields, generated from `syntax.txt` at build time (`scripts/gen_syntax_schema.py`). A `kindmap` group is its type's class, so `match: BinaryExpressionSyntax` does what the planned `kind: [...]` lists would have | the rule docs (P2) explain the two forms of name: a bare kind, or a `…Syntax` type for all its kinds |
| P1-7 | A token is a `Token` struct (`text`, `raw`, `kind`, `missing`, `line`, `col`), not a string, so rules write `node.name.text`. slang's tokens hold string views, so the engine's string values became views (`Reflect.h`) | the curated model (P1.3) can give plain string names where rules read them most |
| P1-8 | Locations (S-3, syntax half): a node is where its first token is, with macro expansions mapped to where the macro is used (`getFullyExpandedLoc`). A declaration is reported at its name, which is found through `name`, or one level down through `header`, `prototype` or `declaration`. A name inside a macro expansion has extent 0 | the semantic model (P1.3) needs the same rule; P1.7 checks it on the corpora |
| P1-9 | The re-targeting. Of the 203 tests held for P1.2, 199 now run on SV snippets and the SV schema. 1 was dropped: `test_cel_node_linked_root` tested the PSS linker's borrowed view, which the SV syntax tree has no equivalent of. 3 are held: they need a flags enum and `has_flag`. Two P1.1 mistakes came to light and were fixed: `ids-reserved.yaml`'s `SVCHKAC001` was not a valid ID (`SVCH001` is the case now), and the engine still named an example's file `example.pss`. There are 19 new host tests (`tests/host/test_syntax_schema.py`). Totals: 1,258 pass, 7 held | if the curated model has a flags enum (P1.3), its tests replace the 3; else they go when `has_flag` lands (P1.4) |
| P1-10 | slang names four members with CEL reserved words (`package`, `function`, `let`, `var`). The engine's parser accepts them after a `.`, and the schema relies on that | before P2, check this against cel-spec; if it is a divergence, log it in design §4 |

## 7. Log

- **2026-10-15** Plan drafted from `svcheck-design.md` and a survey of pssparser `57c6b14`,
  sphinx-systemverilog and the local corpora. Awaiting review of R-1…R-5.
- **2026-10-15** P0.1 narrowed: no pssparser change. Its `docs/design/` is untracked and stays
  uncommitted; svcheck holds the committed copies.
- **2026-10-15** **P0.1–P0.4 done.** Companions copied, and links to pssparser-only documents
  turned into plain references (there is no public URL). Skeleton, `ivpm.yaml` (slang v11.0,
  fmt 12.1.0, rapidyaml 0.16.0, uvm-core 2020.3.1, pyastbuilder), CMake with an offline
  configure, native `svcheck --dump-diags`, Python package and wheel, 2 smoke tests passing.
  Findings P0-1…P0-6. UVM 1.2 as a second configuration is deferred to P1.7: nothing needs it
  yet. P0.5 (CI) waits for where svcheck publishes; P0.6 waits for the namespace.
- **2026-10-15** User decisions: R-1…R-5 approved; rule-ID prefix **`SVCHK`** (provisional, final at
  P2.0), so **P0.6 done**; wheels publish to PyPI from GitHub, docs from Forgejo. P0.5 active.
- **2026-10-15** P0 committed (`0c8dc9c`). P0.5: CI pair (`.github/` + `.forgejo/workflows/ci.yml`, from
  the DVKit templates) and `scripts/ci/build-wheel.sh`. Both pass when trial-run locally in the
  manylinux_2_28 and Forgejo images (P0-7). Waiting on a push to get a green run on the forges.
- **2026-10-15** `main` pushed (`dfd92a3`). GitHub CI green; Forgejo's run not confirmed (private repo).
- **2026-10-15** **P1.1 done.** The engine is forked from pssparser `57c6b14` into `src/engine/` and
  builds as `svcheck_engine` with no slang include path. Cuts: a runtime schema the host
  installs (C9.1), a host function library (C9.2), host-supplied waiver tokens (C9.3), and a
  `run::Context` in place of the PSS model (C9.4). Lineage is in design §3.1, divergences in §4.
  1033 tests pass; 207 are held back for P1.2 and P1.5 (P1-5). Findings P1-1…P1-5.
- **2026-10-15** P1.1 committed (`5aae887`, not pushed). **P1.2 started.**
- **2026-10-15** **P1.2 done.** The syntax schema is generated from slang's `syntax.txt` and
  installed by the SV host (`src/host/`, `svcheck_host`). The node hooks are back, over SV
  snippets; a `ClassDeclaration` rule fires on one. 199 of the 203 held tests run on SV; 1 was
  dropped, 3 are held for a flags enum. 1,258 tests pass, 7 are held, locally and in a
  manylinux_2_28 trial of `build-wheel.sh` (wheel 4.3 MB). Findings P1-6…P1-10.
