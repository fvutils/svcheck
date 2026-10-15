# svcheck: design

| | |
|---|---|
| **Status** | Draft, 2026-10-15. Direction agreed in discussion; nothing is built. |
| **Home** | `svcheck/docs/design/`, with its SV companions (moved from pssparser 2026-10-15, §13). |
| **What** | `svcheck` checks SystemVerilog, and UVM testbenches first, against declarative rules (`sv-rules`: YAML plus CEL), built on slang, with its own rule engine modelled on pssparser's. |
| **Supersedes** | Nothing. Consolidates the SV material in `sv-lint-survey.md`, `sv-lint-demand.md`, `sv-uvm-rules-assessment.md`, `sv-uvm-elaboration-checks.md` and `model-query-database.md` into one design; those stay as the detailed record. |

## 0. Summary

**svcheck** is a SystemVerilog checker whose rules are data:
- **Rules** are files with an `sv-rules:` root, written in YAML with CEL expressions. They are
  evaluated over slang's semantic model by svcheck's own C++ rule engine. The engine follows
  pssparser's design and may start from copies of its code, but it does not depend on
  pssparser (§3.1).
- **The first built-in content is a UVM rule pack.** About three quarters of published UVM
  guidance fits the format directly, and a 36-rule consensus starter pack is identified.
- **What sets it apart from every SV linter surveyed:**
  - semantic rules as data, which can be loaded without running code;
  - the same results in the CLI, CI and (later) the editor;
  - a **run-0 elaboration** of the testbench (component tree, config_db, factory overrides,
    TLM), so rules can catch bugs that UVM otherwise reports only at run time, or never.

**Scope is deliberately checking only.** The elaboration engine and the facts it builds are
internal. A queryable database, a query CLI and an MCP query tool come later, possibly as a
separate tool (`svelab`) in a separate organisation (`sv-insight`). §1 explains why, and the
design keeps that split cheap (§3.4).

**Implementation language: C++** (decided 2026-10-15, §6). Rule evaluation reads slang's C++
model directly, the interpreter is fast, and a browser build stays possible. It is also the
same language as pssparser's engine, so combining the two later remains an option. Python remains the packaging and binding layer, as for pssparser.

## 1. Mission and scope

> **svcheck makes a SystemVerilog testbench's conventions and structure machine-checkable,
> with rules written as data.**

**In scope:**
- methodology rules (UVM first), house style, and library-usage rules carried by libraries
  and IP;
- semantic checks over slang's model: types, inheritance, resolution and constant values;
- checks over the testbench's **run-0 structure**, as an internal means to those rules;
- a deterministic guardrail for AI agents that generate testbench code (sv-lint-demand §3).

**Out of scope:**
- **General code intelligence** (go-to-definition, completion, hover): slang-server, Verible
  and DVT own it.
- **A public query surface over the model:** deferred to `svelab` (§1.1).
- **RTL sign-off lint:** CDC, synthesis semantics and power stay with SpyGlass, Ascent and
  their peers.
- **Formatting:** verible-verilog-format.
- **Simulation, coverage databases, waveforms.**

**The test for a new feature:** is it about checking the conventions or structure of the
verification environment? If it isn't, it belongs elsewhere.

### 1.1 Names and organisation

| Name | Meaning | Status |
|---|---|---|
| `sv-rules` | the rule file format (root key) | decided 2026-10-10 |
| `svcheck` | the checker: CLI, Python package, rule packs | decided 2026-10-10, narrowed 2026-10-15 to "the checker" |
| `svelab` | a later tool: the run-0 elaboration engine, its facts database and queries, split out of svcheck when it is worth using on its own | tentative 2026-10-15 |
| `sv-insight` | a GitHub org for svelab, svcheck and later query/MCP front ends (insight into connections and structure from an SV description) | tentative 2026-10-15; create it when svelab is split, not before |

Availability on 2026-10-15:
- `svcheck`, `svelab`, `sv-insight` and `sv-rules` are free on PyPI and npm.
- `svelab` and `sv-insight` are free on GitHub. Four unrelated repositories are named
  `svcheck`.
- An unrelated PyPI package `svinsight` (a social vulnerability index) exists.
- PyPI names are registered at each tool's first real release. Holding names with empty
  releases goes against PyPI's policy.

**Until then, svcheck lives in `fvutils`**, alongside sphinx-systemverilog.

## 2. Decisions so far

| Date | Decision | Where argued |
|---|---|---|
| 2026-10-10 | Rule files for SV use their own root key, `sv-rules:`, with the same keys under it as `pss-rules:` | sv-lint-survey §5.2 (Q-SV2) |
| 2026-10-10 | Names: `sv-rules` = the format, `svcheck` = the slang-based tool | sv-lint-demand §6 |
| 2026-10-15 | **Model UVM's public API** (intrinsics) in the elaboration engine; interpret the user's code, never `uvm_pkg`'s source | sv-uvm-elaboration-checks §3.2 (Q-EL1) |
| 2026-10-15 | **Checking first**; queries and the database later, probably as `svelab` in `sv-insight` (tentative) | this document §1 |
| 2026-10-15 | **C++** for the checker core, on slang's C++ API; Python for packaging and bindings | this document §6 |
| 2026-10-15 | **Own engine, inspired by pssparser's**, with no source *dependency*. Each project can innovate freely; generalise or combine later | this document §3.1 (user) |
| 2026-10-15 | **Built-in rule IDs use the prefix `SVCHK`** (provisional, one prefix for all built-in packs; final at svcheck-plan P2.0). `SVC` was rejected because it reads as "service" | §11 Q-SC2 (user) |
| 2026-10-15 | **Engine fork (svcheck-plan R-1, R-2):** fork pssparser's whole PSS-free engine from one revision, `57c6b14`, in the spike | svcheck-plan §2 (user) |
| 2026-10-15 | **Q-SC3:** `.f` files only through release 1 (svcheck-plan R-4) | svcheck-plan §2 (user) |
| 2026-10-15 | **Q-SC4:** port level 1 to C++ in SC2, with sphinx-systemverilog's JSON as the oracle (svcheck-plan R-3) | svcheck-plan §2 (user) |
| 2026-10-15 | **Releases:** wheels to PyPI from GitHub; docs to dvkit.org from Forgejo | svcheck-plan P0.5 (user) |
| 2026-10-15 | **pssparser code may be copied literally** as recorded forks (lineage table, §3.1) | Q-SC1 (user) |

## 3. Architecture

```
            ┌──────────────────────── svcheck ─────────────────────────┐
 files ───► │  slang (pinned)  ──►  SV host binding  ──►  rule engine  │ ──► findings
 .f / flow  │  parse, elaborate     schema, handles,      (own; after  │     (text, JSON,
 defines    │                       function library      pssparser's) │      SARIF)
            │                           ▲                              │
            │                           │ facets (internal)            │
            │                     svcheck/elab: level 1 extraction,    │
            │                     level 2 run-0 interpreter, cache     │
            └──────────────────────────────────────────────────────────┘
              Python: wheel, CLI shell, bindings   (later: WASM, editor)
```

**Dependency rule:** the rule engine knows nothing about SV. The SV host binding knows nothing
about specific rules. `elab` knows nothing about rules or the rule engine. That third rule is
what lets `elab` become `svelab` by moving it, not by rewriting it.

### 3.1 The rule engine: svcheck's own, modelled on pssparser's

**Decided (user, 2026-10-15):** for SC1, svcheck is *inspired* by pssparser's checking engine
but does not reuse its source. This avoids a cross-project dependency and lets each project
innovate freely. Generalising or combining comes later.

**What carries over is the design, not the code.** pssparser's engine (`src/lint`, about 18.5k
lines of C++, lint-core-plan C0–C8) is the reference, and its documents are the
specification svcheck starts from:

| Area | pssparser reference | svcheck starts from |
|---|---|---|
| File format | `pss-rules-format.md` | the same keys under `sv-rules:`; differences go in the divergence log (§4) |
| Expression language | `lint-rule-expressions.md`: the CEL subset, value model, null navigation as an error (D-C6), narrowing, linear-time regex, bounds | the same profile |
| Runner | lint-core-plan C5: walk once, dispatch by kind, budgets, failure gate, de-duplicated findings | the same model |
| Pipeline | C7: selection, profiles, per-path overrides, waivers, catalogue, SARIF | the same concepts; waiver *syntax* is SV comments |
| Tests | C5.5: examples-as-tests harness | the same convention |
| Lessons | C0 findings, C8 performance, the known-issues register | read before building the matching part |

**Guardrails, so combining later stays cheap:**
- **Same layering.** The engine holds no SV types. It talks to the host through a schema,
  node handles and a function registry. That is the boundary pssparser's C9 would create on
  its side.
- **A divergence log.** Every deliberate difference from `pss-rules` (format, CEL profile,
  function semantics, CLI) is recorded in §4 with its reason. When the engines are compared,
  that log is the list of questions.
- **Copying is allowed; depending is not** (Q-SC1, decided 2026-10-15). Code may be copied
  from pssparser literally where it fits. Likely candidates: the linear-time regex engine, the
  YAML loader with positions, the CEL parser, checker and evaluator, and the runner. A copy is
  a **fork**: svcheck owns it from then on, with no obligation to stay in sync in either
  direction. Each copy is recorded in the **lineage table** below, so a later comparison knows
  what started shared.

| svcheck path | Copied from (pssparser path @ revision) | Date | Changed since |
|---|---|---|---|
| *(none yet)* | | | |

**pssparser's C9** (separating its own core from PSS) is no longer a prerequisite. It stays in
lint-core-plan as a deferred item for when the engines are combined.

### 3.2 The SV host binding

This is the slang-specific half (sv-lint-survey §4):

| Piece | Source |
|---|---|
| **Syntax schema** | generated from slang's `scripts/syntax.txt` (data-driven, as pssparser's comes from `ast/*.yaml`); `kindmap` groups become `kind: [...]` lists |
| **Semantic schema** | an accessor table for `Symbol`, `Expression`, `Statement`, `Type` and `TimingControl` kinds. The kinds are enumerated, and slang's `ASTSerializer` already visits every property, so the table can largely be generated from it |
| **Node handles** | kind, field get, children, parent, identity, location. Locations map macro expansions to the use site (slang's `SourceManager`); see S-3 in §7 |
| **Function library** | below |
| **Snippet compilation** for `examples:` | a default context that includes `uvm_pkg` (compiled once and cached), so UVM examples are short |

**Function library** (sv-uvm-rules-assessment §2.4):

| Group | Functions | Notes |
|---|---|---|
| Navigation (core) | `parent`, `ancestors`, `children`, `descendants`, `enclosing`, `siblings`, `index_of`, `isa` | same meaning as for PSS |
| Symbols, types, constants | `resolves_to`, `decl_path`, `in_library`, `type_of` (`isClass`, `isVirtualInterface`, `isFourState`, `bitWidth`), `const_value` (including strings), `type_arg(t, i)` | slang resolves expressions to symbols directly |
| Classes | `derives_from`, `owner(subroutine)`, `member(c, name)`, `members(c)`, `overrides(m)`, `calls(n)`, `is_super_call(c)` | `overrides` is `SubroutineSymbol::getOverride` |
| Expressions | `access_root(e)` | the variable at the root of `a.b.c[i]`; used for "assigns through a virtual interface" |
| Locations | `file_of`, `decls_in`, `includes(file)` / `included_by(file)` | the include graph has `compilation` scope |
| Preprocessor | `MacroUse` kind + `macro_of(n)`, `Directive` kind | Q-UVM2: is `MacroUse` a `match:` kind or only reachable through `macro_of`? |
| Ordering | `before(n)`: earlier statements in the same block chain, straight-line only | deliberately not dataflow |
| Strings | `format_specs(str)` | optional; slang already checks format strings |
| Elaboration (internal, §3.4) | facet kinds and functions (`UvmInstance`, `ConfigGet`, `scope_matches`, …) | inferred per rule, below |

**Nothing in the library is UVM-specific.** UVM's vocabulary (roles, phase names, registries,
the config_db API) is written as YAML helpers in the pack (§5).

### 3.3 Scopes and degraded mode

As in rules-over-models §3.3, every function, and so every rule, has a computed scope:

| Scope | Needs |
|---|---|
| `file` | one file, parsed alone |
| `compilation` | the file list, defines and include paths, and therefore `uvm_pkg` |
| `model` | the elaborated design |
| `facet:uvm` | the run-0 elaboration (§3.4); level 1 or level 2 |

When the context is too small for a rule, the rule is reported **not run**, once. It is never
run on partial facts (answers Q-SV4). Most UVM rules need `compilation`, because
`derives_from(c, "uvm_pkg::uvm_driver")` must resolve. Verible-style single-file linting
cannot serve them.

### 3.4 Elaboration (`svcheck/elab`, internal)

The full design is `sv-uvm-elaboration-checks.md`. In short:
- **A facet** is a named derived model with a generated schema. The loader infers which facets
  a rule needs from the kinds and functions it uses, as it already infers `level` and `scope`.
  The engine builds a facet only when a selected rule needs it.
- **Level 1, type-level extraction:** what each class builds, as written.
  - Creation sites, with exact types through the `uvm_component_registry #(T, Tname)`
    specialisation.
  - `connect` edges, config_db accesses, factory overrides and vif bindings.
  - sphinx-systemverilog's `model/uvm.py` implements this today in Python (751 tests, real
    UVM 1800.2-2020.3.1); svcheck ports it to C++.
- **Level 2, per-test run-0 interpretation:**
  - It executes the top module's `initial` blocks up to `run_test`, then constructors,
    `build_phase` (top-down, siblings in name order), `connect_phase` and the
    end-of-elaboration phases, for each test.
  - UVM's public API is modelled by **intrinsics**: factory, config_db with resource
    precedence per UVM version, component naming, `connect`, objections, `start`, and
    `uvm_reg*` building.
  - Values are constant-or-unknown. A branch on an unknown runs both ways and marks results
    `may`. An unknown loop bound gives multiplicity `*`. Virtual calls use the class
    hierarchy.
  - DPI, file I/O, unmodelled calls and exhausted budgets become recorded **havoc points**. A
    rule that concludes absence must test completeness.
- **Roots:** every concrete `uvm_test`, or a configured list. Findings are de-duplicated by
  location, and the message names the tests.
- **Cache:** facets may be cached between runs, keyed by inputs, with per-root read sets for
  invalidation (model-query-database §3.1–§3.3). **Private:** no stable schema, no query
  surface. Making it public is svelab's job.

**What level 2 adds** (sv-uvm-elaboration-checks §2, §6):
- config `get` with no matching `set`, and `set` that nothing reads;
- `set`/`get` type mismatches, and wrong-scope `set`s;
- overrides issued after the create they target;
- sequences started on the wrong sequencer type;
- unconnected ports, and analysis ports with no subscriber;
- tests that raise no objection;
- two active agents on one interface; backdoor HDL paths that don't exist in the RTL.

### 3.5 Front ends

| Front end | When | Notes |
|---|---|---|
| CLI `svcheck` | first | options and selection as in pssparser (`-W`, profiles, `--rules`, `--check-rules`, `--explain`, SARIF/JSON) |
| Python package | first | the wheel ships the CLI and bindings: findings, catalogue, rule validation |
| Config | first | `svcheck.toml`, or a `[svcheck]` table in a project file; the file list from `.f` files, FuseSoC or `flow.yaml` (Q-SC3) |
| Editor (LSP) and WASM | later | slang has official WASI support; an Emscripten/browser build and its size are untested (§7) |
| MCP **check** tool | later | `check(files)`, `explain(rule)`, `rules_for(file)` (rules-over-models §5.3). A **query** tool belongs to svelab |

### 3.6 A curated SV model, defined with pyastbuilder (proposed 2026-10-15)

**The question (user, 2026-10-15):** should pyastbuilder define a clean "above parse tree" view
of SystemVerilog, instead of rules binding to slang's own types?

**Proposal: yes, for the semantic layer. Keep slang's syntax tree for syntax rules.**

| Layer | What rules see | Defined by |
|---|---|---|
| **Syntax** | slang's syntax tree, complete | a schema generated from slang's `scripts/syntax.txt`, as planned (§3.2) |
| **Semantic model** (new) | a curated, documented SV model: packages, classes, methods, statements, expressions, interfaces with modports and clocking blocks, a module-hierarchy summary, covergroups and constraints at a shallow level. Names are resolved to declarations, macro provenance is a field, and specialisations are explicit | **`ast/*.yaml` in pyastbuilder**, as pssparser's AST is. It is populated by a hand-written C++ lowering pass from slang's semantic model (the analogue of pssparser's `AstBuilder`) |
| **Facets** | `UvmInstance`, `ConfigSet`, `ConfigGet`, `FactoryOverride`, … (§3.4) | **the same YAML** |

**Why:**
1. **Rule stability is the contract.** Rule packs are public and carried by libraries
   (§5.2), so the kinds and fields they name must outlive slang upgrades (S-5). A schema we
   own is that contract. A slang change then touches only the lowering pass, never a rule.
2. **Everything pyastbuilder already generates comes free.** This makes the hand-maintained
   semantic accessor table of §3.2, and its risk, disappear:
   - **reflection tables** for the rule engine (the same path pssparser's engine uses, lint-core-plan C1.3);
   - **Python bindings**, so sphinx-systemverilog could read svcheck's model instead of walking
     pyslang (§6);
   - **TS/WASM bindings** for the editor build;
   - **Sphinx reference docs and relationship diagrams** for every node kind. Rule authors
     get a documented node reference, as `pss-rules` authors have since 2026-10-11.
   - **Later**, the facts database's table definitions (model-query-database §3.4), from one
     more generator.
3. **The model can be designed for checking.**
   - Constructs that are many shapes in slang (registration via macro or by hand, the five
     override call forms) can be normalised once in the lowering, not in every rule.
   - Testbench concepts (role, registry type, macro of origin) can be fields.
4. **One toolchain for `pss-rules` and `sv-rules`:** the same YAML conventions, docs
   generator, reflection and snapshot gates.

**Costs and how to contain them:**

| Cost | Containment |
|---|---|
| A second copy of the model (time, memory) | lower **user code fully** and **library code (uvm_pkg, VIP) as declarations only**: signatures, base classes, members, no bodies. `uvm_pkg` alone is 324k semantic nodes, and most of them are bodies |
| SV is large; a complete schema is a big design | curate the subset rules need. Anything else lowers to `Opaque { slangKind, span }`, so nothing is dropped silently (sphinx-systemverilog's rule) |
| Two sources of truth for semantics | never recompute: lowered nodes carry slang's resolution, and facts that are cheap to precompute (declared types, constant values). Each keeps a back-handle to its slang symbol, so library functions (`type_of`, `const_value`, `overrides`) can ask slang lazily |
| Syntax-level rules can't reach curated-away detail | they use the syntax layer, which is complete |
| pyastbuilder maturity (0.0.8; AB-1..AB-3 open, AB-2 Python factory construction) | it is our tool and pssparser already depends on it. SV adds weight to fixing AB-2 |

**Prior art in our own tree:** zuspec-fe-sv (`zuspec/zuspec-fe-sv`, Python on pyslang, about
3k lines, January 2026) already lowers class-based SV into Zuspec IR, for an executable
2-state subset. Its target differs (an IR to run, not a model to check, so it rejects
4-state code and modules), but its class, statement and function mappers show what the
lowering involves. If svcheck's model exists, zuspec-fe-sv could later lower from it instead
of from pyslang. That is a convergence option, not a goal.

**Decide in the spike (SC1):**
- Build the curated model for the subset the spike's 10 rules need.
- Measure lowering time and memory on the corpora.
- Write the rules against it.
- **Adopt it if** lowering costs under about 20% of slang's compile+bind time and the rules
  come out no longer than against slang directly (Q-SC5).

## 4. The rule format: `sv-rules`

It starts identical to `pss-rules` (pss-rules-format §2–§6). The table below is the
**divergence log** (§3.1): every deliberate difference, with its reason. Add to it whenever the
format, the CEL profile or a function's meaning departs from pssparser's.

| Difference | Why |
|---|---|
| Root key `sv-rules:` | one schema per host (decided) |
| `match:` kinds are slang's syntax kinds, the curated model's kinds (§3.6) and facet kinds | generated schemas (§3.2, §3.6) |
| **`uses:` (helpers shared between files) is needed in v1** | every UVM rule uses the shared vocabulary (`uvm.yaml`); without it each file repeats about 20 lines. For PSS this is deferred Q-F2 |
| **File-level options** are needed with `uses:` | a role helper reads an option (`driver_pattern`) |
| Examples compile in a default context with `uvm_pkg` | short UVM examples |
| Namespaces | built-in SV rules need a namespace that is not an EDA standard name. `UVM` is IEEE 1800.2's, so probably not that (Q-UVM5) |

Example (sv-uvm-rules-assessment §4 has eleven):

```yaml
sv-rules:
  version: 1
  uses: [uvm.yaml]
  rules:
    - id: SVCHK001                 # illustrative ID
      name: unregistered-uvm-class
      tier: suspicious
      family: factory
      summary: A UVM object or component class is not registered with the factory
      message: >-
        '${node.name}' is a ${is(node, "uvm_component") ? "uvm_component" : "uvm_object"}
        but is not registered; type_id::create and factory overrides cannot see it
      match: { kind: ClassType }
      where: >-
        !in_library(node) && !node.isAbstract && is(node, "uvm_object") &&
        member(node, "type_id") == null
```

The rule checks what the registration macro *produces* (a `type_id` typedef), not the macro's
name. A hand-written registration or a house wrapper macro passes too.

## 5. Built-in content

### 5.1 The UVM pack

From `sv-uvm-rules-assessment.md`:
- 173 rules consolidated from 14 methodology sources, 9 style guides and the tool catalogues.
- Tier A, expressible with the generic library: 130 (75%). Tier B, one new host function:
  24. Tier C, whole-model: 8. Tier D, not lint: 11, plus formatting.

| Part | Content |
|---|---|
| `uvm.yaml` | the vocabulary: roles (`is_driver`, `is_monitor`, `is_sequence`, `is_test`, …), phase names, `factory_create`, `config_db` helpers |
| **Consensus starter pack (36 rules)** | the first release. Tier A, supported by at least three sources or two tools, no source against: F1–F3, F5, F7, F9, F11, F13, F16; K1, K2, K4, K14; T1, T3, T8; S2, S5, S6, S9; P1, P3, P4, P8, P10, P13, P14; G2, G3, G6; A4, A7, A12; L3, L7, L12 |
| Level-1 facet rules | orphan config set/get by field, broken override, vif binding unread or undriven, non-`this` parent |
| Level-2 facet rules | the list in §3.4 |
| **Presets** | `uvm-consensus`, `easier-uvm`, `lowrisc-dv`, `core-v`. The guides disagree on about 15 points (field macros, `super.build_phase`, runtime phases, objection ownership, `` `uvm_warning ``, naming suffixes; assessment §5.2). Each conflict is an **option on one rule**, and a preset is a configuration fragment, not a new kernel concept |

### 5.2 Other content (later)

- **A Verible-equivalent pack.** About 50 of Verible's 61 rules are expressible as data
  (sv-lint-survey §3). A few are enough to check equivalence in the spike. The full set is
  only worth porting if users ask.
- **slang's own warnings** are mapped into selection, waivers and SARIF under their own
  namespace, not reimplemented (Q-UVM4).
- **Library-carried packs** (fw-hdl, VIPs, Caliptra integration rules) use the same format
  and are loaded from the project's dependencies (Q-SV5).

## 6. Implementation language: C++ (decided 2026-10-15)

**Measured 2026-10-15** with pyslang 11.0.0 on the Accellera UVM 1800.2-2020 library alone
(`uvm_pkg.sv`):

| Step | Time |
|---|---|
| Compile + all diagnostics | 0.12 s |
| Walk all 323,646 semantic nodes, empty Python callback | 0.19 s (≈0.6 µs/node) |
| Same walk, with an `isinstance` test and one field read per node | 1.17 s (≈3.6 µs/node) |

The third row is the floor for a rule engine written in Python. A real testbench is several
times `uvm_pkg`'s size, and real rules read several fields per node, so a Python rule pass
lands in seconds to tens of seconds: fine for docs, marginal for CI, wrong for an editor.

**Why C++:**
1. **Direct access to slang.** Rule evaluation reads node fields constantly. In C++ that is a
   direct call into slang's model; from Python it is a crossing per field access, which is the
   slow path rules-over-models §3.1 already flagged. Being C++ also keeps a later combination
   with pssparser's C++ engine possible (§3.1).
2. **The interpreter** (level 2) is the heavy part: symbolic `build_phase` for every test.
   An interpreter written in Python over pyslang is slow twice over.
3. **Editor and browser.** pssparser already ships a TS/WASM build, and slang builds for WASI.
   Python rules that out.
4. **The infrastructure exists.** C++ + CMake + ivpm + Python wheels + npm are already in place
   for pssparser.

**Where Python stays:**
- the wheel, the CLI shell and the bindings;
- **sphinx-systemverilog's provider as the reference** for level 1, with its fixtures reused
  in the conformance corpus (§9);
- exploration of slang's API through pyslang.

Later, sphinx-systemverilog could read svcheck/svelab's model through those bindings instead
of walking pyslang itself, which leaves one model implementation.

**The cost is iteration speed on the interpreter's semantics.** It is offset by a conformance
corpus that does not depend on the implementation language (§9). The interpreter is not
prototyped in Python first.

## 7. Host risks

| ID | Risk | Plan |
|---|---|---|
| S-1 | **Elaboration multiplicity.** A module body is elaborated once per parameter set; a level-2 root is per test | de-duplicate findings by location; messages name the instances or tests |
| S-2 | **Generic class bodies.** slang builds a `ClassType` per specialisation, and uninstantiated generics are not elaborated (pyslang: 174 generic definitions in `uvm_pkg` alone) | run class rules on each generic body once; level 2 gets specialisations from the `create` sites it reaches. Spike question |
| S-3 | **Macros.** Registration, messaging, sequences and field automation are all macros | locations at the use site; `MacroUse`/`macro_of`; a finding inside a `uvm_pkg` macro is reported at the use site or suppressed |
| S-4 | **Compilation context.** UVM rules need the file list, `+incdir`, defines and the UVM version | read `.f` files and FuseSoC/`flow.yaml`; rules that can't run are reported **not run** |
| S-5 | **slang API churn** | pin one slang version per svcheck release through ivpm; regenerate the syntax schema from `syntax.txt`; the semantic accessor table is checked by tests |
| S-6 | **Library vs user code** | `in_library` covers `uvm_pkg` and configured VIP globs; rules skip library code by default |
| S-7 | **UVM model fidelity** (level 2) | the simulator dump as oracle (§9); UVM version as an option |
| S-8 | **WASM size and speed** | measured in the spike; not a release-1 requirement |

## 8. Repository layout (proposed)

```
svcheck/
  CMakeLists.txt  ivpm.yaml  pyproject.toml
  src/
    ast/             # pyastbuilder YAML: the curated SV model + facet kinds (§3.6)
    lower/           # slang semantic model → curated model
    host/            # SV host binding: schema tables, node handles, locations
      schema/        # generated from slang syntax.txt + semantic accessor table
      lib/           # function library (§3.2)
    elab/            # level 1 extraction, level 2 interpreter, intrinsics, cache
    cli/             # native entry point
  python/svcheck/    # wheel: CLI shell, bindings, config loading
  rules/
    uvm.yaml         # shared vocabulary (§5.1)
    uvm-*.yaml       # the UVM pack, by family
    presets/         # uvm-consensus, easier-uvm, lowrisc-dv, core-v
  scripts/           # schema generators
  tests/
    rules/           # examples-as-tests run every rule's examples
    elab/            # conformance corpus: testbench + expected facets
    corpora/         # pinned external testbenches (by reference, via ivpm)
  docs/
    design/          # this document and its companions (§13)
```

**Dependencies, all through ivpm:**
- slang (pinned);
- rapidyaml (pssparser's choice, D-C2: positions, WASM);
- no CEL library: the engine has its own parser and evaluator, for pssparser's reasons
  (lint-core-plan D-C1: WASM and no abseil/protobuf dependency);
- UVM releases (for snippet compilation and tests).

## 9. Testing

| Layer | How |
|---|---|
| Rules | every built-in rule has `examples:` with at least one firing case and one near-miss, run by the core's harness (pss-rules-format §4) |
| Verible equivalence | for the rules both tools have, findings match or differ for an explainable semantic reason (spike go/no-go) |
| Elaboration conformance | small testbenches, each with its expected facts. Level 1 expectations come from sphinx-systemverilog's provider (its fixtures and corpus). Level 2 expectations come from a **simulator dump**: a small `uvm_facet_dumper` component run to `end_of_elaboration` (Verilator runs upstream UVM), writing JSON. The interpreter is diffed against it |
| Corpora | `uvm-tb-proto/out` (LLM-generated), `featherweight-ip/fw-wb-dma/tests/uvm`, OpenTitan `hw/ip/*/dv` (with `lowrisc-dv`), CORE-V-VERIF (with `core-v`) |
| Precision | lint-criteria: a rule that misses its tier's precision bar on the corpora goes to `nursery` or `pedantic` |
| Performance | a gate on corpus time, as lint-core C8 does for PSS |
| Agent measurement | does an agent's generated UVM converge on a team's conventions faster with the rules than with the same conventions in a prompt? (sv-lint-demand §5.1). This measures the product thesis, not the engineering |

## 10. Plan

| Phase | What | Exit criterion |
|---|---|---|
| **SC1** spike, about 3–4 weeks | Repo skeleton; a **minimal engine** (§3.1): YAML loader with positions, CEL parse and evaluate, kind-indexed runner, text/JSON output; slang host with the syntax schema; the **curated semantic model** (§3.6) for classes, subroutines, calls, assignments and case statements, defined in pyastbuilder YAML and lowered from slang; `uses:`; 10 rules: 3 Verible-equivalent, the 4 UVM rules UVM001/010/020/030 from assessment §4, 3 from fw-hdl's FWH set; run on the corpora | every rule ≤ 15 lines of predicate; Verible equivalence; S-1/S-2 behaviour understood; native time measured (WASM size noted, not gated); **Q-SC5 decided** on lowering cost |
| **SC2** | The rest of the engine: CEL type checker, selection and profiles, waivers, SARIF, catalogue, examples harness; UVM consensus pack (36) + `uvm.yaml` + presets; level 1 port from sphinx-systemverilog and its facet rules; config, SARIF, `--explain`, catalogue; first release | the starter pack meets the precision bar on the corpora |
| **SC3** | Level 2 interpreter: concrete first (one test, fixed plusargs), then unknowns/may/havoc, all tests; the level-2 rules; simulator-dump oracle | facet diff against the dump is clean on the corpora; the level-2 rules meet the precision bar |
| **SC4** | Cross-model links (vif → RTL instance, HDL paths); Tier B macro rules; Verible-equivalent pack if wanted | as for SC3 |
| **SC5** | Editor/WASM; MCP check tool; agent measurement | measured, as §9 |
| later | **svelab**: move `elab` out, add the public facts database and queries (`model-query-database.md`); create `sv-insight` | a separate decision |

## 11. Open questions

**New in this document:**
- ~~**Q-SC1.**~~ **Decided (user, 2026-10-15):** copying pssparser code literally is
  allowed, as recorded forks; depending on pssparser is not (§3.1).
- **Q-SC2.** *Provisionally decided (user, 2026-10-15): one prefix, `SVCHK`; final at
  svcheck-plan P2.0.* Built-in rule namespace(s): one for the UVM pack, one for generic SV rules, or one
  overall? Avoid EDA standard names (`UVM`, `SV`, `PSL`).
- **Q-SC3.** *Decided (user, 2026-10-15): `.f` files only through release 1 (svcheck-plan R-4).*
  The project-file format for the compilation context: `.f` files, FuseSoC,
  `flow.yaml`, or all three in release 1?
- **Q-SC5.** Should the semantic layer rules see be a curated model defined with pyastbuilder
  and lowered from slang (§3.6, proposed), or slang's semantic types through an accessor table
  (§3.2)? Decided by measurement in SC1.
- **Q-SC4.** *Decided (user, 2026-10-15): port to C++ in SC2, JSON as the oracle (svcheck-plan R-3).*
  Is the level-1 port to C++ worth it before level 2, or should level 1 come from
  sphinx-systemverilog as JSON through the facet loader until level 2 lands? The schema is the
  same either way.

**Carried over** (answered ones omitted; see the source documents):
- **Q-SV1:** when to build (now answered in principle: SC1).
- **Q-SV3:** fw-hdl's compiler running the same pack.
- **Q-SV5:** distribution of library-carried rules.
- **Q-UVM1:** roles as YAML helpers (recommended).
- **Q-UVM2:** `MacroUse` as a kind or a function.
- **Q-UVM3:** whether presets ship with the pack.
- **Q-UVM4:** slang warnings in their own namespace.
- **Q-UVM5:** pack namespace (now Q-SC2).
- **Q-EL2:** facets as a core concept, shared with PSS for `exec init`.
- **Q-EL3:** default roots.
- **Q-EL4:** partial-in-tests reporting.
- **Q-EL5:** whether the dump producer ships.
- **Q-EL6:** the shared model package, now svelab.
- **Q-DB1–5:** deferred with svelab.

## 12. References

**Companion documents** (this directory; written in pssparser 2026-10-10 to 2026-10-15):
- `sv-lint-survey.md`: SV linters and custom rules; Verible's rules as data; the SV host
- `sv-lint-demand.md`: demand; the UVM-pack and agent-guardrail positioning; naming
- `sv-uvm-rules-assessment.md` and `sv-uvm-rules-research/`: the 173-rule catalogue, tiers,
  conflicts, worked rules, and the raw research with source URLs
- `sv-uvm-elaboration-checks.md`: facets, the run-0 interpreter, cross-model checks
- `model-query-database.md`: the facts database (svelab, later)
- `rules-over-models.md`: the model contract, scopes, packs, the agent surface

**pssparser's engine design** (pssparser `docs/design/`; a reference, not a dependency): `pss-rules-format.md`,
`lint-rule-expressions.md`, `lint-core-plan.md` (C0–C8; C9 deferred), `lint-criteria.md`,
`portable-lint-rules.md`.

**Related tools:**
- sphinx-systemverilog (`fvutils/sphinx-systemverilog`): `model/uvm.py`,
  `docs/design/uvm-testbench-elaboration.md`
- slang (`MikePopoloski/slang`), Verible, svlint, AMIQ DVT/Verissimo, Sigasi

## 13. Moving to the svcheck repository

**Done 2026-10-15** (svcheck-plan P0.1), as copies: pssparser's `docs/design/` is untracked
and is not changed, so step 2's pointers do not apply. The original steps:
1. **Move** this file to `svcheck/docs/design/svcheck-design.md`.
2. **Copy** the SV-specific companions with it:
   - `sv-uvm-rules-assessment.md`
   - `sv-uvm-rules-research/`
   - `sv-uvm-elaboration-checks.md`
   - `model-query-database.md`
   - `sv-lint-survey.md` and `sv-lint-demand.md`

   In pssparser, leave a one-line pointer in each moved file's place.
3. **Keep** the rule-core documents in pssparser, and link to them by URL.
4. **Leave C9** (the core separation) in pssparser's `lint-core-plan.md`, deferred until the
   engines are combined.
