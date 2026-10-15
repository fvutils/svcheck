# Checking what UVM builds at run time, before it runs

Status: design note, 2026-10-15. Nothing here is decided.
Follows [`sv-uvm-rules-assessment.md`](sv-uvm-rules-assessment.md), whose Tiers C and D were
largely "needs the run-time component tree". The model contract (scopes, multi-model runs) is
pssparser `docs/design/rules-over-models.md` §3 and §6.5.

**The idea (user, 2026-10-15):** if we know what elaboration a check needs, we can often build
that elaboration ourselves, by unrolling and analysing the procedural code. Most of what is
otherwise checked only at run time then becomes checkable statically.

## 0. Answer

> **Yes, and UVM is close to the ideal case.** A UVM testbench builds its whole structure in
> zero simulated time:
> - the component tree, factory overrides, config_db traffic and TLM connections;
> - the virtual-interface bindings and the register maps.
>
> The code that does it is straight-line class code: `build_phase`, `connect_phase` and
> constructors, plus the `initial` block that calls `run_test`. It is driven by constants,
> config objects and loops with known bounds. Executing just that part (a **"run 0"**) gives a
> model that rules can query like any other.
>
> **What it buys.** Of the 19 Tier C/D rows in the assessment, 11 become precise (§6). It also
> opens a new class of rules for bugs UVM reports only at run time, or never (§2):
> - a config `set` that nothing reads;
> - an override issued after the create it was meant for;
> - a sequence started on a sequencer of the wrong type;
> - a test that raises no objection, so it ends at time 0 and passes.
>
> **The design in one paragraph.** An *elaboration* is a named derived model, a **facet**
> (`uvm`), with its own generated schema (§4.4). A rule never says it needs a facet: the loader
> infers it from the kinds and functions the rule uses, exactly as `level` and `scope` are
> inferred today. The engine builds a facet only when a selected rule needs it.
>
> A facet has one schema and several possible **producers**:
> - *extraction* per type, as written: this exists today in sphinx-systemverilog (level 1);
> - an *interpreter* in the SV host, which evaluates the run-0 code with unknown values allowed
>   (level 2);
> - a *dump* from a real simulator run, which is the oracle for testing the interpreter.
>
> Facet nodes point back to source (`at`) and to slang's hierarchy, so the same mechanism gives
> the first real **cross-model** rules: UVM tree × RTL hierarchy × register description (§5).
>
> **Prior art.**
> - **sphinx-systemverilog already does the first half.** Its `model/uvm.py` (U0–U6,
>   2026-08-29; 751 tests, verified against real UVM 1800.2-2020.3.1) extracts, per class, *as
>   written*:
>   - creation sites (exact types through the registry specialisation);
>   - `connect` edges, config_db sets and gets, and factory overrides;
>   - vif bindings joined to the RTL.
>
>   It already reports orphan sets and gets, broken overrides, and undriven or orphan
>   bindings (§1.1).
> - **What it deliberately stops short of is the per-test instance tree.** Without the tree,
>   config scopes can't be matched, overrides can't be applied, and loop counts stay unknown.
>   That missing step is exactly what this note proposes.
> - **AMIQ's DVT IDE** has done a concrete "run 0" of one chosen test since 2023, with its own
>   engine and no simulator. It shows the result as views; nothing public connects it to
>   Verissimo's rules.
> - **What is new here** is rules over the elaborated model, as data, across every test at once,
>   with unknowns kept rather than chosen.
>   Docs and lint read the same model: sphinx-systemverilog's "orphan set" row and a lint
>   finding are the same fact.

## 1. Why this generalises the earlier tiers

The assessment classed rules by what they *read*:
- the syntax or semantic tree (Tier A);
- plus a helper (Tier B);
- the whole model (Tier C);
- the run (Tier D).

This note adds a dimension: what must be **evaluated**. Three levels exist already:

| Elaboration | Who does it today | Example rule input |
|---|---|---|
| Parse | slang | syntax |
| Static elaboration (parameters, generate, module instances) | slang | the module hierarchy, constant values |
| PSS linking/elaboration | pssparser | resolved types, component tree |

The proposal adds a fourth, **program-driven elaboration**: structure that the language
builds by *running code*, which no compiler front end computes:

| Language | Built by code at time 0 | Today checked |
|---|---|---|
| SV/UVM | component tree, factory, config_db, TLM, vifs, register maps | at run time, by UVM, partially |
| SystemC | module tree, port bindings (in `sc_main` and constructors) | at run time, by the kernel |
| PSS | register handles and base addresses (`exec init_down/up`), pool bindings | at solve or run time |
| cocotb / Python TBs | everything | at run time |

UVM comes first because the run-0 code is the most stereotyped, and the payoff is the largest.

### 1.1 What sphinx-systemverilog already has (`fvutils/sphinx-systemverilog`, `model/uvm.py`)

Read 2026-10-15 from its source and `docs/design/uvm-testbench-elaboration.md`.

| Capability | How | Status |
|---|---|---|
| Created type of `T::type_id::create` | `Tname`/`T` of the `uvm_component_registry #(T, Tname)` specialisation behind `create`; exact, through the macro | done |
| `new()`-constructed components and ports | `NewClass` expression, type from the left-hand side | done |
| Component tree, keyed by **type** (what each class builds) | walk of `build_phase` statements; conditional and dynamic flagged, never guessed | done |
| Three node states: `ok`, `opaque` (black-boxed), `unresolved` (`<error>` type) | nothing is silently dropped | done; this is §3.3's completeness, already practised |
| TLM edges from `connect_phase` | `CallExpression.thisClass` + argument `MemberAccess` chains | done |
| config_db `set`/`get` | type parameter, `cntxt` text, literal `inst_name`/`field` (or "computed"), conditional | done |
| set↔get pairing, orphan sets/gets | **field name only**, glob in both directions; scope is shown but unused | done |
| Factory overrides | all five call shapes, per-method argument order; **annotated, never applied**; `broken` = names an unknown type | done |
| vif bridge | `set` value → elaborated interface instance → RTL instances on its ports; `orphan`, `undriven`, `unresolved` | done |
| Sequence activity | `body()` statements, macro-expanded, filtered | done |
| `run_test("name")` | default root | done |

**Its stated boundary** is the point where this note starts. It describes "the hierarchy as
written in `build_phase`, not the hierarchy that will exist at run time", and it keys on type
because "`this` inside `cfg_agent` is a different string in every testbench". The consequences
are deliberate and are what per-root interpretation removes:

| Limited by type-keyed extraction | Becomes, with per-root instance paths |
|---|---|
| config pairing on field name only, so a **right field, wrong scope** `set` looks consumed | scope globbing against concrete instance paths: the wrong-scope set is found |
| overrides annotated, not applied | applied in execution order; "issued after create" and "never applies" become decidable |
| loop multiplicity "dynamic" (slang does not fold even a `localparam` bound) | folded when the bound is known (via config objects) |
| a conditional child shown as conditional | resolved per test when the condition is known (`is_active` from the test's config) |
| generic classes nothing specialises are not elaborated | specialisations come from the `create` sites the interpreter reaches |

It also records hard-won facts the interpreter must not relearn:
- **Override argument order differs by declaring class.** `uvm_component` puts the path
  first; `uvm_factory` puts it last.
- The registry override form names its original only through `Tname`.
- `CallExpression.type` reports the generic `T`, so read the registry parameters instead.
- slang leaves loop bounds unfolded; `EvalContext` exists for this.
- Real UVM compiles in about 0.1 s with pyslang 11; eagerly forcing every method body in
  `uvm_pkg` takes 0.36 s.

## 2. What UVM checks at run time, or never

| Defect | What UVM does | Static, with the facet |
|---|---|---|
| `config_db::get` with no matching `set` | fatal at time 0 if the result is checked; silent default otherwise | exact per test and instance path |
| `set` that no `get` consumes (typo in field or path, stale scope) | silent; `check_config_usage()` or `+UVM_CONFIG_DB_TRACE` only if someone asks | exact, when every get is known |
| `set`/`get` type mismatch (`uvm_object` vs the concrete class, `int` vs `bit[31:0]`) | `get` returns 0; silent unless checked | exact |
| A `set` shadowed by a higher-precedence `set` | silent | exact, once the precedence rules are modelled |
| Factory override issued after the instance was created | silent: the override just doesn't apply | exact (execution order is known) |
| Override names a type that is never created | silent | exact |
| Duplicate child name | run-time report (`BDCLD`) | exact |
| Port not connected (driver `seq_item_port`) | fails at the first `get_next_item` | exact |
| Analysis port with no subscriber (monitor → nothing) | silent: analysis ports allow zero connections | exact |
| Port connected to the wrong kind or count | error at end of elaboration | exact |
| Sequence started on a sequencer that is not its `p_sequencer` type | fatal at `start` | exact where the start site is in run-0 or straight-line run code |
| No objection raised in any run-time phase | the test ends at time 0 and **passes** | per test, when every run task is known (§3.4) |
| vif not set for an agent | fatal at time 0 (if the agent checks) | exact |
| Two active agents driving one interface instance | contention, found by debugging | cross-model (§5) |
| `uvm_hdl_*`/backdoor path that does not exist in the RTL | error on first access | cross-model, against slang's hierarchy |
| Register map overlaps; map has no sequencer/adapter | reported at lock or on the first access | exact from the unrolled `build()` |
| Component class never instantiated by any test | nothing | exact across all tests |

The **silent** rows are the valuable ones. Each is a real class of debugging session, and
several are the subject of the papers in the assessment's sources (Cooper & Marriott and
Edelman on config_db; Verilab on overrides).

## 3. The interpreter

### 3.1 What is executed

Per test, in UVM's own order:

1. The `initial` blocks of the top module(s) up to and including `run_test()`. These do vif
   `set`s, `set_timeout`, and sometimes factory or config setup.
2. `run_test(name)` selects the root. `name` comes from `+UVM_TESTNAME`, or from the argument.
3. **build**, top-down: the constructor, then `build_phase`, of each component, children in
   UVM's traversal order. `m_children` is keyed by name, so siblings go in name order. That
   matters for config precedence and override timing.
4. **connect** (bottom-up), `end_of_elaboration`, `start_of_simulation`.
5. Optionally, **run-phase structure** (§3.4). This is not a simulation; it is a summary of what
   each run-time task *starts* and *raises*.

Nothing after time 0 is simulated. The interpreter needs functions, `new`, assignments,
`if`/`case`/loops, `$sformatf`, string methods, class handles, queues, dynamic and associative
arrays, and virtual dispatch.

### 3.2 The UVM library: model it, don't interpret it

There are two ways to get UVM semantics:

- **Interpret `uvm_pkg` itself** (probably DVT's route; its engine runs real code).
  - Most faithful to each UVM version.
  - But the library's internals are string-keyed associative arrays, regex matching in the
    resource pool, and callbacks, which are hostile to abstraction (§3.3).
- **Intrinsics**: model the public API in the host.
  - The API: `type_id::create`, `set_*_override*`, `uvm_config_db::set/get`, the
    `uvm_component` constructor and naming, `connect`, `raise/drop_objection`, `start`, and the
    `uvm_reg*` building calls.
  - The model keeps factory, resource and tree state as first-class facts with source anchors.

**Decided (user, 2026-10-15): intrinsics.** The user's code is interpreted; `uvm_pkg` is not.
- A call into `uvm_pkg` that has no intrinsic becomes a havoc point (§3.3). It is reported, not
  guessed.
- The resource-precedence rules are the subtle part. They are a UVM version option
  (`[sv.uvm] version = "1.2" | "2017" | "2020"`).
- They are tested against the real library by the dump producer (§4.2).

### 3.3 Unknowns: concrete where possible, not guessed where not

The value domain is **constant-or-unknown** per variable: a flat lattice, i.e. constant
propagation run as an interpreter. Plus:

| Source of unknown | Treatment |
|---|---|
| `randomize()` of a config object | its rand fields become unknown. Later: domains from simple constraints, e.g. `num_agents inside {[1:4]}` gives a small set, so it can be enumerated |
| `$value$plusargs`, `$test$plusargs` | unknown, unless a **run configuration** supplies plusargs per test (from the regression list, `flow.yaml`) |
| `if`/`case` on an unknown | **both** branches run; everything created inside is marked `may` instead of `must` |
| Loop with an unknown bound | one symbolic iteration; the instances get multiplicity `*` and an index-free path (`env.agent[*]`) |
| Virtual call on an object of unknown dynamic type | all overrides in the class hierarchy (CHA), joined |
| DPI, file I/O, `uvm_pkg` call without an intrinsic, budget exhausted | **havoc**: the effect is unknown; recorded with its location |

Every facet records its havoc points. That makes **completeness** queryable: "no `set` matches
this `get`" may only be concluded when no havoc point could have performed a matching `set`. A
rule that concludes *absence* must test completeness, or it would break the precision bar
(lint-criteria). §4.4 shows the field.

**Budgets** (lint-rule-expressions §5) apply per test: steps, unroll limit and call depth. When
a budget runs out, the remainder becomes havoc, never an error.

### 3.4 Run-phase structure, without simulating

Two of the best rules ("no objection raised", "sequence on the wrong sequencer") need the run
phase, but only its *structure*:
- which tasks are forked;
- which sequences each starts, and on which sequencer handle;
- whether a `raise_objection` is reachable before the first blocking statement.

That is a summary per task, computed by the same interpreter with time-consuming statements
(`#`, `@`, `wait`, blocking TLM calls) treated as opaque and every branch explored. It is
deliberately an *over-approximation of reachability*. Exact objection balance on all paths
stays out of scope, as in the assessment (P17).

### 3.5 Roots and multiplicity

A facet is built **per root**: per test class × run configuration.
- **Default roots:** every concrete `uvm_test` subclass. An option names the tests, or reads
  them from the regression list.
- **The same source line can yield findings in several roots.** Findings are de-duplicated by
  rule, location and message, and the message names the roots ("in 3 of 12 tests: smoke,
  dma_burst, …"). This is S-1 (elaboration multiplicity) one level up.
- **The interpreter also resolves S-2 (generic class bodies) for every class it reaches.** A
  `create` names a concrete specialisation, so `my_agent #(axi_cfg)` is interpreted with
  `axi_cfg`.

## 4. Facets in the rule engine

### 4.1 What a rule needs is inferred, as today

The loader already computes each rule's `level` and `scope` from the functions it calls
(pss-rules-format §3.1). This adds one computed attribute:

- **`facets`**: the set of derived models whose kinds or functions the rule uses.
  - `match: { kind: ConfigGet }` means `facets: [uvm]`.
  - So does calling `uvm_instances(c)`.
- `--explain` shows it.
- A facet that cannot be built (no file list, no `uvm_pkg`, no tests) makes the rule
  **not run**, reported once, as for an unmet scope (rules-over-models §3.3).

There is no `requires:` key to write and none to get wrong. That is the user's "if we know what
elaboration is required by a check", answered by the loader rather than by the author.

### 4.2 One schema, several producers

| Producer | How | Precision | Cost | Use |
|---|---|---|---|---|
| **Extraction** (exists: sphinx-systemverilog `UvmProvider`) | per type, as written; pyslang | type-level: no paths, overrides not applied, field-only pairing | already built | facet level 1 now: orphan set/get by field, broken overrides, vif bridge faults, non-`this` parents |
| **Interpreter** (§3) | in the SV host, over slang's AST | all tests at once; unknowns kept; havoc recorded | no simulator, seconds | the product: CLI, CI, editor, agents |
| **Dump** | a small SV component (`uvm_facet_dumper`) runs to `end_of_elaboration` in any simulator (Verilator now runs upstream UVM) and writes the facet as JSON | exact for one test and seed; no `may` | a simulator run per test | oracle for the interpreter; the fallback for testbenches the interpreter can't handle |
| **Concrete interpreter** | §3 with unknowns forbidden (fixed plusargs and seed) | as the dump | no simulator | DVT-equivalent debugging views; not needed for rules |

Rules don't know which producer ran, but they do know the facet's **level**:
- **level 1**: type-keyed, from extraction;
- **level 2**: per root, from the interpreter or a dump.

A rule that reads `UvmInstance.path` needs level 2. The loader infers that the same way it
infers `facets`, and the rule is reported as not run when only level 1 is available.
Extraction is the cheapest way to start, since it exists (§8, E0). It lets
rules be written and judged on real testbenches before the interpreter exists, and it remains
the interpreter's regression oracle afterwards: run both on a corpus and diff the facets.

### 4.3 Locations and explanations

Every facet node carries:
- **`at`**: the source construct responsible, such as the `create` call or the `set` call.
  Findings are reported there.
- **`trace`**: the elaboration call path that led to it (`base_test.build_phase` →
  `env.build_phase` → `agent.build_phase:42`). It maps to SARIF `codeFlows`, so an editor can
  show *why* `env.agent[1].drv` exists, or why it is an `axi_drv_err`.

### 4.4 The `uvm` facet schema (draft)

Generated like any other schema (rules-over-models §3.1). Kinds:

| Kind | Fields |
|---|---|
| `UvmRoot` | `test` (ClassType), `run_config`, `havoc` (list), `complete` (bool) |
| `UvmInstance` | `path`, `name`, `type` (ClassType, after overrides), `requested` (ClassType, before overrides), `parent`, `children`, `certainty` (`must`/`may`), `multiplicity`, `at`, `trace` |
| `FactoryOverride` | `original`, `replacement`, `inst_pattern` (opt), `by_name`, `at`, `applied_to` (list of `UvmInstance`), `issued_after` (list of `UvmInstance` already created that it would have matched) |
| `ConfigSet` | `scope` (pattern, resolved against the context's path), `field`, `type`, `value` (opt), `at`, `consumed_by` (list of `ConfigGet`), `shadowed_by` (list of `ConfigSet`) |
| `ConfigGet` | `instance`, `field`, `type`, `at`, `matched` (list of `ConfigSet`, in precedence order), `checked` (result tested), `complete` |
| `TlmPort` | `instance`, `member`, `kind` (port/export/imp, analysis or not), `if_type`, `connections`, `min`, `max` |
| `VifBinding` | `get` (ConfigGet), `target` (slang interface instance, cross-model §5) |
| `SequenceStart` | `sequence` (ClassType), `sequencer` (UvmInstance or unknown), `at`, `in_phase` |
| `ObjectionSite` | `instance`, `phase`, `raise`/`drop`, `reachable_before_block` (bool) |
| `RegBlock` / `RegMap` / `RegEntry` | `path`, `offset`, `size`, `access`, `hdl_paths`, `map.sequencer`, `map.adapter` |

### 4.5 Rules over the facet

The same format, the same CEL. The `match:` kinds are facet kinds:

```yaml
sv-rules:
  version: 1
  rules:
    - id: UVMC101
      name: config-get-unsatisfied
      tier: correctness
      summary: A config_db get that no set satisfies
      message: >-
        ${node.instance.path} gets '${node.field}' (${node.type.name}) but nothing sets it
        in test ${root.test.name}${node.checked ? "; the test will stop with a fatal" :
        ", and the result is not checked: the variable keeps its default"}
      match: { kind: ConfigGet }
      where: node.complete && node.matched.size() == 0

    - id: UVMC102
      name: config-set-unused
      tier: suspicious
      summary: A config_db set that no get reads
      message: >-
        set of '${node.field}' for '${node.scope}' is never read in test ${root.test.name};
        check the field name and the scope
      match: { kind: ConfigSet }
      where: root.complete && node.consumed_by.size() == 0

    - id: UVMC103
      name: config-type-mismatch
      tier: correctness
      summary: A get finds a set of the same name with a different type
      message: >-
        '${node.field}' is set as ${s.type.name} (${s.at}) but got as ${node.type.name};
        uvm_config_db matches on type, so this get fails
      match: { kind: ConfigGet }
      let:
        s: >-
          all_of(ConfigSet).filter(x, x.field == node.field &&
            scope_matches(x.scope, node.instance.path) && x.type != node.type)
      report: s.size() > 0 ? [node] : []

    - id: UVMC110
      name: override-after-create
      tier: correctness
      summary: A factory override is issued after the instance it targets was created
      message: >-
        override ${node.original.name} → ${node.replacement.name} comes after
        ${node.issued_after[0].path} was created; it has no effect on that instance
      match: { kind: FactoryOverride }
      where: node.issued_after.size() > 0

    - id: UVMC120
      name: analysis-port-unsubscribed
      tier: suspicious
      summary: An analysis port has no subscriber in this test
      message: "${node.instance.path}.${node.member} writes to nobody in test ${root.test.name}"
      match: { kind: TlmPort }
      where: node.kind == "analysis_port" && node.connections.size() == 0 && root.complete

    - id: UVMC130
      name: test-without-objection
      tier: correctness
      summary: No objection is raised in any run-time phase, so the test ends at time 0
      message: "test ${node.test.name} raises no objection; it ends at time 0 and passes"
      match: { kind: UvmRoot }
      where: >-
        node.complete && !all_of(ObjectionSite).exists(o, o.raise && o.reachable_before_block)
```

`root` is bound to the `UvmRoot` the node belongs to. `all_of` is scoped to that root.
`scope_matches` is UVM's glob/regex path matching, exposed as a function, because rules
should never reimplement it.

## 5. Cross-model checks

The facet holds handles into *other* models, so the facet is where the models meet:

| Link | From | To | Rules it enables |
|---|---|---|---|
| vif value | `ConfigSet.value` of a `virtual X_if` set in the top module (`top.u_axi_if`) | slang's elaborated interface instance | two *active* agents bound to one interface instance; an interface instance on a DUT port with no agent; a passive-only interface that nothing drives; modport consistency |
| HDL paths | `uvm_hdl_*` string arguments, `add_hdl_path`, `RegEntry.hdl_paths` | slang's hierarchy (`Symbol::getHierarchicalPath`) | a backdoor path that doesn't exist; a path to a net that is not the register's storage (heuristic) |
| Register map | `RegEntry` offsets and sizes from the unrolled `build()` | SystemRDL / IP-XACT (when loaded), RTL address decode (later) | the UVM model is out of date with the register description; overlaps; missing registers |
| Tests | `UvmRoot` set | the regression list (`flow.yaml`) | tests never run; listed tests that don't exist |

This is the "multi-model run" that rules-over-models §6.5 said not to build "until a real rule
needs it". The vif and HDL-path rules are those real rules.
- **The vif and HDL-path links need nothing new.** Both models (the slang compilation and the
  `uvm` facet) come from one compilation, and the facet stores slang symbol handles directly.
- **Only the register-description link needs the general mechanism**: node handles that carry
  their model identity (rules-over-models §3.1).

## 6. Effect on the assessment

Rows of `sv-uvm-rules-assessment.md` §3 that move:

| Row | Was | With the `uvm` facet |
|---|---|---|
| F15 override before create (across the hierarchy) | B / D | exact (UVMC110) |
| G10 every get has a set | C, constant keys only | exact per test, including computed keys with known values (UVMC101) |
| G11 set/get type match | C | exact (UVMC103) |
| G13 config tree; a component gets only its own | D | checkable: `ConfigSet.scope` vs the setter's subtree |
| A15 all TLM ports connected | C | exact |
| A16 one agent per DUT interface | D | cross-model (§5) |
| P17 objections balanced | D | still D for balance; "raised at all" becomes checkable (UVMC130) |
| P18 timeout set | C | exact per test |
| S16 vsequencer handles null-checked | D | replaced by checking that the handles are *set* in every test |
| R5 backdoor needs an HDL path | D | cross-model (§5) |
| K16 one base test | C | exact |

**New rules with no row in the assessment**, because no guide states them; they are what UVM
reports at run time: UVMC102 (set unused), shadowed set, duplicate child name, override never
applied, sequence on the wrong sequencer, unsubscribed analysis port, test without an
objection, class never instantiated, register overlaps, and register map without a sequencer or
adapter.

That is **about 11 moved rows and 10 new rules**. Tiers C and D shrink from 19 rows to 8, and
those 8 are genuinely dataflow, judgment or formatting.

## 7. Limits and risks

| Risk | Mitigation |
|---|---|
| **Fidelity of the UVM model.** The intrinsics must match the library: resource precedence, name-ordered traversal, override chains, `set` during build vs after. | The dump producer as oracle on a corpus (§4.2); a version option; conformance tests generated from the real library |
| **Testbenches that build from data** (plusargs, files, DPI, randomized topology) | unknowns and havoc keep findings sound; run configurations supply plusargs; rules that conclude absence require `complete` |
| **Interpreter size.** An SV class-code interpreter is real work: expressions, statements, handles, containers, strings, virtual dispatch. | slang already has a constant evaluator for functions (`EvalContext`), which covers expressions and statements but not class objects. Extend or wrap it rather than start over. Guess: 5–8k lines of C++ for the interpreter plus intrinsics |
| **Test count.** 200 tests × a large testbench | Most tests are the base test plus overrides. Memoise per class and per known config: the env subtree is usually identical across tests. Build roots lazily in the editor (the current test only) |
| **Editor latency** | facets are `model` scope: rebuilt on save, not per keystroke; cached per root |
| **UVMF/generated testbenches** use deep config indirection | the best test case, not a risk: UVMF is YAML-generated, so the facet can also be compared with the generator's input |

## 8. Order of work (proposed)

| Step | What | Answers |
|---|---|---|
| **E0** | Facet schema; **level 1 from sphinx-systemverilog's `UvmProvider`** (the type-keyed rules: orphan set/get by field, broken override, vif orphan/undriven, non-`this` parent); run on `uvm-tb-proto/out`, `fw-wb-dma/tests/uvm` and the sphinx-systemverilog corpus | Are facet rules worth having? Precision on real code. Needs no new extraction. |
| **E0b** | The dump producer (an SV component plus JSON) for level 2; UVMC101–130 against it | The value of the per-root rules, before building the interpreter |
| **E1** | Concrete interpreter for run-0 (one test, plusargs and seed fixed), intrinsics for factory, config_db, tree, connect; diff against E0 | Can we reproduce UVM's elaboration without a simulator? |
| **E2** | Unknowns, may/must, havoc and completeness, all tests at once | Does it stay precise when nothing is fixed? |
| **E3** | Cross-model links: vif → slang instance, HDL paths | The first cross-model rules |
| **E4** | Run-phase structure (sequence starts, objection reachability); register models | UVMC130, sequencer typing, register map rules |

E0 is small (days): the extraction exists. E0b is about a week. Together they test the
*value* of facet rules before the expensive part (E1–E2) is built.

**Where the interpreter lives** (Q-EL6) follows from E0. sphinx-systemverilog's provider is the
natural base for it: Python over pyslang, already correct on the awkward cases. Extract it into
a package that both the docs tool and the lint host depend on, so docs and lint read one model.
That is the "model written in Python" case of rules-over-models §3.1. A C++ port is worth doing
only if the editor or WASM build needs the facet without Python. That is the same order the demand report recommended for the SV host as a whole.

## 9. Open questions

The constructed model is also worth persisting and querying outside lint; see
[`model-query-database.md`](model-query-database.md).

- ~~**Q-EL1.**~~ **Decided (user, 2026-10-15): model the public API (intrinsics)**, as §3.2
  recommends. The user code is interpreted; `uvm_pkg` is not.
- **Q-EL2.** Should facets be a kernel concept (named, with a schema and producers, inferred
  per rule), or a feature of the SV host only? A kernel concept also serves PSS: `exec init`
  evaluation for register base addresses, and pool binding.
- **Q-EL3.** Default roots: every concrete `uvm_test`, or only tests in a regression list?
- **Q-EL4.** Should a finding that holds in some tests but not others be reported differently
  (lower severity, or "in 3 of 12 tests")?
- **Q-EL5.** Should the dump producer ship as a supported facet source (for testbenches the
  interpreter cannot handle), or stay a test oracle only?
- **Q-EL6.** Should sphinx-systemverilog's UVM provider be extracted into a shared package
  (e.g. `uvm-model`) that both sphinx-systemverilog and the lint host use, and should the
  interpreter grow inside it (recommended)? Or should the lint host get its own C++
  implementation?

## Sources

- AMIQ, [Runtime UVM Elaboration in the DVT Eclipse IDE](https://eda.amiq.com/tutorials/runtime-uvm-elaboration-in-the-dvt-eclipse-ide);
  [press release](https://eda.amiq.com/press-releases/amiq-eda-adds-runtime-elaboration-of-uvm-testbench-code-to-dvt-eclipse-ide);
  SemiWiki, [Runtime Elaboration of UVM Verification Code](https://semiwiki.com/eda/365069-runtime-elaboration-of-uvm-verification-code/)
  (2025-12-30). Its "run 0" uses DVT's own execution engine, one chosen test at a time, with
  DPI-C support. It shows hierarchy, factory, config_db and register views. No public link to
  lint rules.
- The config_db failure modes: Cooper & Marriott, *Demystifying the UVM Configuration
  Database* (DVCon 2014); Edelman, *Go Figure* and *Avoiding Configuration Madness*; see
  `sv-uvm-rules-research/a-methodology-guidelines.md`.
