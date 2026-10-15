# UVM and SystemVerilog testbench rules as data: what fits `sv-rules`

Status: research report, 2026-10-15. Nothing here is decided.
Follows [`sv-lint-survey.md`](sv-lint-survey.md) (an SV host over slang for the `pss-rules`
kernel) and [`sv-lint-demand.md`](sv-lint-demand.md) (lead with a UVM pack; §5.2 test 2,
"encode 20 Easier-UVM/cookbook guidelines", is this exercise at full scale). The format and
expression language are pssparser `docs/design/pss-rules-format.md` and
pssparser `docs/design/lint-rule-expressions.md`.

Raw research, with every guideline and source URL:
- [`sv-uvm-rules-research/a-methodology-guidelines.md`](sv-uvm-rules-research/a-methodology-guidelines.md):
  128 guidelines from 14 methodology sources (Easier UVM, the Mentor cookbook,
  Sutherland/Fitzpatrick, Verilab, Cummings, Edelman, and others).
- [`sv-uvm-rules-research/b-style-guides.md`](sv-uvm-rules-research/b-style-guides.md):
  146 rules from 9 style guides (lowRISC DV, OpenTitan, CORE-V-VERIF, systemverilog.io,
  Zwolinski, and others).
- [`sv-uvm-rules-research/c-tool-catalogues.md`](sv-uvm-rules-research/c-tool-catalogues.md):
  what the tools check (Verissimo, Sigasi, draconian_uvm, Verible, svlint, AsFigo, slang and
  Verilator warnings) and how they take custom rules.

## 0. Answer

> **Three quarters of the UVM guidance (130 of 173 rules) fits `sv-rules` YAML as it stands.**
> That needs only the generic SV function library the survey already planned (§4 there) plus
> **four** new functions. No function needs to know about UVM. UVM itself (the roles, phase
> names, registries and the config_db API) can be written as YAML helpers.
>
> The consolidated catalogue (§3) has 173 rules, plus 6 groups (mostly formatting) that are
> not lint at all:
>
> | Tier | Rules | What it takes |
> |---|---:|---|
> | **A**: data, with the generic SV library | 130 (75%) | `match:` + `where:` over slang's semantic AST: `derives_from`, `type_of`, `const_value`, `enclosing`, `overrides`, `calls`, `file_of` |
> | **B**: data, plus one new host function | 24 (14%) | macro provenance, straight-line statement order, include and directive view |
> | **C**: whole-model data, approximate | 8 (5%) | string-keyed cross-reference (config_db set↔get, include graph, package cycles); constant keys only |
> | **D**: not lint | 11 (6%) | needs the run-time component tree, path-sensitive dataflow or judgment, or is formatting |
>
> **For "quickly create a set of rules for well-structured UVM":**
> - 82 of the Tier A rules are **consensus rules**: no source contradicts them. §5.1 picks the
>   36 with the broadest support as a starter pack. Each rule's predicate is about 3–6
>   lines of YAML (§4).
> - The rest split into **profiles**, because the guides contradict each other on about a dozen
>   points (§5.2). Examples: field macros, `super.build_phase`, runtime phases, `uvm_warning`,
>   naming suffixes, and who raises objections. These become rule options and option presets
>   (`lowrisc`, `easier-uvm`, `core-v`), not separate rules.
>
> **What decides whether this works is not the rule format.** It is three host questions:
> - **Macro provenance (S-3).** Registration, messaging, sequences and field automation are all
>   macros.
> - **Generic class bodies (S-2).** Most UVM components are parameterised.
> - **Compiling against `uvm_pkg`.**
>
> All three are spike items (§6). Running rules on the *expanded* code turns out to be an
> asset: "is this class registered?" becomes "does it have a `type_id` of the right registry
> type?". That is more robust than any macro-name match, and it catches hand-written
> registrations too (§2.2).

## 1. Sources

| Kind | Sources | Notes |
|---|---|---|
| Methodology | Doulos Easier UVM (≈100 guidelines, 19 sections); Mentor/Siemens UVM Cookbook appendix (2013 PDF; online version needs a login); Sutherland & Fitzpatrick *UVM Rapid Adoption* (DVCon 2015); Erickson *Are OVM & UVM macros evil?* (DVCon 2011); Verilab tutorial, config-DB and register papers; Edelman's three config/objection papers; Spear's config_db guidelines; Cummings on event regions, test termination and 1800.2 | The *why*; most guidelines have a rationale |
| Style guides | lowRISC `DVCodingStyle.md` (the most complete public DV guide); OpenTitan `cip_lib`/`dv_lib`/`uvmdvgen`; CORE-V-VERIF `CodingStyleGuidelines.md` (derived from Hunter's *Advanced UVM*); systemverilog.io; Zwolinski's MISRA-style testbench guide | Ibex, CVA6, PULP and Caliptra defer to lowRISC or just run Verible |
| Tools | AMIQ Verissimo (≈670 rules; ≈200 UVM/TB; rule pool and Java API not public); Sigasi (10 UVM rules, 145–154; configurable, not extensible); draconian_uvm (Python, line regex); Verible (C++); svlint (Rust plug-ins; `verifintent` preset); AsFigo PySlint/cnstlint/fcovlint (Python over pyslang/Verible JSON); slang and Verilator warnings | The *what is already checked* |

**What the survey adds to the earlier reports:**
- **No tool lets a user write a semantic UVM rule as data.**
  - The nearest things are **Verissimo's ruleset XML**: *generic templates* (`Build Calls
    Super` with `baseClass` and `methodName` properties) and an **architecture model** (a
    role such as DRIVER = base class `uvm_driver` + name pattern `.*_drv`).
  - Both ideas are reusable here, as helpers and options (§2.1).
  - ast-grep YAML over tree-sitter-systemverilog is the only declarative precedent, and it is
    syntax-only.
- **The guides contradict each other often enough that a pack must be configurable** (§5.2).
  Every serious team has a house style, which is exactly the "local style for AI agents"
  argument in `sv-lint-demand.md`.
- **Commercial tools publish nothing at rule level** for testbench code (Synopsys, Cadence,
  Siemens, Blue Pearl). Verissimo is the one dedicated product.

## 2. How UVM maps onto the rule format

### 2.1 Roles are helpers, not host code

Most UVM rules are scoped by **role**: in a driver, in a sequence, in a test. Verissimo
hard-codes the roles in its architecture model. Here a role is a one-line helper over
`derives_from`, with an optional name pattern as an option, so a team whose drivers don't
extend `uvm_driver` can still be checked:

```yaml
define:
  is(c, base): c != null && derives_from(c, "uvm_pkg::" + base)
  cls(n): enclosing(n, ClassType)
  is_driver(c):    is(c, "uvm_driver") || (c != null && c.name.matches(options.driver_pattern))
  is_monitor(c):   is(c, "uvm_monitor")
  is_sequence(c):  is(c, "uvm_sequence_base")
  is_item(c):      is(c, "uvm_sequence_item") && !is(c, "uvm_sequence_base")
  is_test(c):      is(c, "uvm_test")
  in_method(n, names): enclosing(n, Subroutine) != null && enclosing(n, Subroutine).name in names
  PHASES: >-
    ["build_phase","connect_phase","end_of_elaboration_phase","start_of_simulation_phase",
     "run_phase","extract_phase","check_phase","report_phase","final_phase",
     "pre_reset_phase","reset_phase","post_reset_phase","pre_configure_phase",
     "configure_phase","post_configure_phase","pre_main_phase","main_phase","post_main_phase",
     "pre_shutdown_phase","shutdown_phase","post_shutdown_phase"]
```

Every UVM rule uses these, so they belong in **one shared helpers file** (`uvm.yaml`). That
needs the deferred `uses:` key (pss-rules-format Q-F2). For an SV UVM pack, Q-F2 moves from
"later" to "needed first". Without it, every rule file repeats about 20 lines.
`is_driver` reads an option, so a shared helpers file also needs **file-level options** (today
options belong to a rule). That is a second small format extension, and it belongs with Q-F2.

There is one design choice: ship the role predicates as YAML helpers, or as host functions
(`uvm_role(c)`). **Recommendation: YAML.** That keeps the host free of UVM, lets a team
override a role, and keeps the same path open for in-house base-class libraries (OpenTitan's
`dv_base_*` and `cip_base_*`, fw-hdl, UVMF).

### 2.2 Macros: check the expansion where possible, the macro use where not

UVM is macro-heavy. Slang expands macros, so a rule sees the code the macro produced. That
splits macro-related rules three ways:

| Approach | Rules | How |
|---|---|---|
| **Check the expansion** (Tier A) | registration present (`` `uvm_*_utils `` makes a `typedef uvm_component_registry #(T,"T") type_id`); registration flavour and type argument; message verbosity and ID (the `` `uvm_info `` expansion calls `uvm_report_enabled(VERBOSITY, UVM_INFO, ID)`) | A semantic rule on what the macro *made*. It works for hand-written equivalents and for wrapper macros (`` `cmn_info ``, `` `DV_CHECK ``), and it needs no macro names |
| **Check the macro use** (Tier B) | no `` `uvm_do* ``/`` `uvm_send ``/`` `uvm_create ``; field-macro policy; `` `uvm_warning `` ban; `` `uvm_declare_p_sequencer `` vs a hand-written `p_sequencer`; `;` after a `` `uvm_* `` macro; required wrapper (`` `DV_CHECK_RANDOMIZE_FATAL ``) | The expansion is indistinguishable from hand-written code, so the rule needs the macro use itself. One host feature serves them all: a `MacroUse` pseudo-node kind (name, arguments, span, `expansion`) plus `macro_of(node)` → the innermost macro a node was expanded from (optional) |
| **Exclude expansions** (Tier B) | "no direct `uvm_report_*` calls" (the macros expand to exactly these calls) | `!macro_of(node).hasValue()` |

`macro_of` is also how the S-3 policy (findings inside expansions) gets implemented: by
default a finding inside a `uvm_pkg` macro expansion is reported at the use site, or
suppressed when the macro is the thing the rule is about.

### 2.3 Strings carry meaning

config_db field names, instance-path scopes, factory names (`create("drv", this)`), plusarg
names and report IDs are all **strings**. Many rules (wildcards, absolute paths, name≠variable,
`uvm_` plusarg prefix, `"default_sequence"`) are therefore `const_value(arg)` plus
`matches()`. That is Tier A, as long as the string is a constant. A computed string
(`$sformatf`) is *unknown*, and the rule must stay silent rather than guess. For
`create()` names in loops, lowRISC wants `$sformatf("arr[%0d]", i)`; there the rule accepts a
non-constant name.

### 2.4 The function library this needs

**From the survey's planned generic SV host** (no change):
- `derives_from`, `type_of` (with `isClass`, `isVirtualInterface`, `isFourState`,
  `bitWidth`), `const_value` (including strings), `resolves_to`
- `enclosing`, `ancestors`, `siblings`, `parent`, `in_library`
- `type_arg(t, i)`, `file_of`, `decls_in`

**Generic SV functions the survey implied but did not list.** Each is small and backed
directly by slang:

| Function | Meaning | slang |
|---|---|---|
| `owner(subroutine)` | the class that declares a method, e.g. the `uvm_config_db #(T)` specialisation of a `get` | `SubroutineSymbol::getParentScope` |
| `member(c, name)` / `members(c)` | a class's own members (optional / list) | `Scope::find`, member iteration |
| `overrides(m)` | the base-class method `m` overrides (optional) | `SubroutineSymbol::getOverride` |
| `calls(n)` | call expressions under `n` (not into callees) | AST walk |
| `is_super_call(c)` | `super.f(...)` | `CallExpression` with a `super` receiver |
| `access_root(e)` | the variable at the root of `a.b.c[i]`: for "assigns through a vif" | walk `MemberAccess`/`ElementSelect` |

**New (Tier B), and why:**

| Function | Needed by | Cost |
|---|---|---|
| `MacroUse` kind + `macro_of(n)` | §2.2's macro-use rules | from slang's `SourceManager` expansion locations; syntax-tree trivia for the use |
| `before(n)` → earlier statements of the same block chain, straight-line (no branch merging) | "randomize before start", "override before create", "`set` a handle that was created", "raise before first delay" | a statement-list walk; deliberately not dataflow |
| `includes(file)` / `included_by(file)`, `Directive` kind | include-once, include-only-from-package, include guards, `` `endif `` comments, local macro `` `undef `` | slang preprocessor records |
| `format_specs(str)` | format-string checks beyond slang's own | small parser; optional because slang already warns |

Path-sensitive analyses ("every raise is dropped on every path", "the vif is null-checked
before use on every path") are **not** proposed as functions in v1. `before()` gives a useful
straight-line approximation, and the rest is Tier D. That is the same line the PSS design
draws (lint-rule-expressions §3: recursion lives in functions; rules only compose).

## 3. The catalogue

Each row consolidates every source that states the rule. **Tier** is as in §0. **C** marks
*consensus* (no source contradicts it); **P** marks a rule that needs an option because sources
disagree (§5.2).

Source keys:
- Guides and papers: EU Easier UVM, MG Mentor cookbook, SF Sutherland/Fitzpatrick,
  AE Erickson, VL Verilab, ED Edelman, SP Spear, CC Cummings, LR lowRISC DV, OT OpenTitan,
  CV CORE-V-VERIF, UG Accellera UVM 1.2 User's Guide.
- Tools: VS Verissimo, SG Sigasi, DU draconian_uvm, VB Verible, SL slang warnings,
  VT Verilator, AF AsFigo.

### 3.1 Factory and registration

| # | Rule | Sources | Tier | | How |
|---|---|---|---|---|---|
| F1 | A `uvm_object`/`uvm_component` subclass is registered | EU MG AE LR SF VS SG | A | P | no `type_id` member; options: exempt abstract classes, config classes (EU) |
| F2 | Registration flavour matches the base (component vs object registry) | SG DU VS | A | C | `type_of(member(c,"type_id"))` derives from `uvm_component_registry` iff `c` is a component |
| F3 | Registry type argument is the enclosing class | SG VS | A | C | `type_arg(registry, 0) == c` |
| F4 | A parameterised class uses `*_param_utils` | LR MG | A | C | class has parameters ∧ registry's name argument ≠ `""` |
| F5 | `new()` of a UVM object instead of `type_id::create` | EU MG VL LR SF SG VS | A | C | `NewClass` whose type `is(…, "uvm_object")`, not `in_library` |
| F6 | TLM ports/FIFOs and covergroups are created with `new()` | SF VL LR | A | C | create of a `uvm_port_base` subtype |
| F7 | `create("x")` name equals the variable | EU MG LR VB SG | A | C | §4 UVM011 |
| F8 | Instance-name strings use only `[A-Za-z0-9_]` (plus `[i]`) | EU VS | A | C | `const_value` + `matches` |
| F9 | Component `create` parent argument is `this` | EU LR SF | A | C | |
| F10 | `create` named on a type parameter | LR | A | C | prefix `resolves_to` a `TypeParameter` |
| F11 | Type-based overrides, not by name | EU VL | A | C | call to `set_type_override_by_name`/`set_inst_override_by_name` |
| F12 | Override type extends the original | VL SG VS | A | C | owner of `set_type_override` vs `get_type()` argument's owner |
| F13 | No global `factory`; use `uvm_factory::get()` | EU VS | A | C | `resolves_to` the `uvm_pkg::factory` variable |
| F14 | Overrides only in tests (and only in `build_phase`) | VS | A | | enclosing class `is_test`, method |
| F15 | Override issued before the create it affects, in the same method | VL LR SF VS | B | C | `before()`; across levels of the hierarchy it is Tier D |
| F16 | Components are created in `build_phase` | EU VL LR VS | A | C | enclosing method; option for helper methods (false positives otherwise) |
| F17 | No hand-written `create()` | CC | A | C | method named `create` in a user class |
| F18 | `create` called outside any phase | VS | A | | subsumed by F16 for components |

### 3.2 Constructors and class structure

| # | Rule | Sources | Tier | | How |
|---|---|---|---|---|---|
| K1 | Object constructor is `new(string name = "")` | EU LR SG VS | A | C | signature + default `const_value` |
| K2 | Component constructor is `(string name, uvm_component parent)` with no defaults | EU LR SG | A | C | |
| K3 | No constructor arguments beyond name/parent | MG LR | A | C | |
| K4 | `super.new` is the first statement | MG LR EU VT | A | C | `body[0]` is a super call |
| K5 | Constructor otherwise empty (covergroup `new` allowed) | EU LR | A | C | |
| K6 | Registration macro position (first vs after members) | EU LR | B | P | `macro_of` + member index |
| K7 | Member section order (macro, ports, fields, `new`, phases, methods) | EU CV | B | P | `index_of` + kinds |
| K8 | `rand` members are not `local`/`protected` | MG CV VS | A | C | |
| K9 | `virtual` repeated on overrides (CV) / omitted (EU) | CV EU | A | P | `overrides(m)` + `isVirtual` keyword; option |
| K10 | A property shadows a base-class property | CV SL | A | C | map slang `shadow-property` |
| K11 | A non-virtual method is hidden by a subclass method | SG VS | A | C | `overrides` absent ∧ same name in a base |
| K12 | `extern` methods (MG) vs no prototypes (CV) | MG CV | A | P | |
| K13 | Undocumented UVM internals (`m_*`, `uvm_top`, `is_active` field) | EU SF VS | A | C | `resolves_to(...)` `in_library` ∧ name in deny list/regex |
| K14 | Deprecated or removed UVM API, by version | EU MG VL LR SG VS CC | A | C | deny list per version as an option (also: compile with `UVM_NO_DEPRECATED`) |
| K15 | Classes are declared in a package | EU MG CV VS | A | C | `enclosing(c, Package)` |
| K16 | Only one class extends `uvm_test` directly (one base test) | CV OT | C | | `all_of(ClassType)` count |

### 3.3 Transactions (sequence items)

| # | Rule | Sources | Tier | | How |
|---|---|---|---|---|---|
| T1 | Items extend `uvm_sequence_item`, never `uvm_transaction` | EU SF LR CV VS | A | C | |
| T2 | Field-macro policy: ban (EU MG AE CC) / items only (LR) | EU MG AE CC LR VS CV | B | P | `MacroUse` name `uvm_field_*` + enclosing class role |
| T3 | Items override `convert2string`, `do_copy`, `do_compare` (+ `do_print`, `do_record`, `do_pack`) | EU SF MG CC CV VS | A | C | `member(c, …)`; list is an option |
| T4 | Override `do_*`, never `copy`/`compare`/`print`/`pack` | CC | A | C | method name in a deny list ∧ `overrides` a `uvm_object` method |
| T5 | `do_copy`/`do_compare` call `super.do_*` | AE | A | C | `calls(m).exists(is_super_call)` |
| T6 | `do_*` don't use the policy argument | EU | A | | no `refs` of the parameter inside the method (local) |
| T7 | No tasks and no 4-state fields in items | VS | A | | |
| T8 | Avoid `print()`/`sprint()`; log with `convert2string` | SF CC LR | A | C | call on a `uvm_object` |
| T9 | Driver doesn't randomize received items | LR | A | C | `randomize` on `req` in an `is_driver` class |
| T10 | Don't modify a transaction received in `write()`: copy first | MG CC VS | B | C | assignment whose `access_root` is the `write` argument; local only |
| T11 | Monitor creates a new item per `ap.write()` | SF | D | | dataflow across a loop |
| T12 | `do_unpack` mirrors `do_pack` order | AE | D | | |
| T13 | `rand` on stimulus, not response fields | EU MG CC | D | | intent |

### 3.4 Sequences

| # | Rule | Sources | Tier | | How |
|---|---|---|---|---|---|
| S1 | No `` `uvm_do* ``, `` `uvm_send ``, `` `uvm_create ``, `` `uvm_rand_send `` | EU MG AE LR CC VS | B | P | `MacroUse`; VL notes late randomization as a use, so it needs an option |
| S2 | `randomize()` result is checked: not discarded, not `void'`, not inside `assert` | EU MG LR CC CV VB VS DU SL VT | A | C | §4 UVM020 |
| S3 | Required randomize wrapper (`` `DV_CHECK_RANDOMIZE_FATAL ``, `` `cmn_assert ``) | LR DU | B | P | `macro_of(call)` in an allowed list |
| S4 | A sequence is randomized before `start()` | EU LR VS | B | C | §4 UVM080 (`before`) |
| S5 | No `default_sequence` | SF AE VL CC VS | A | C | config_db `set` with constant field `"default_sequence"` |
| S6 | `pre_start`/`post_start`, not `pre_body`/`post_body` | EU MG LR | A | C | `overrides` of those in a sequence |
| S7 | No explicit delays in sequences (LR: clock waits allowed) | MG LR | A | P | `Timed` statement / `#` inside `is_sequence`; callees are Tier D |
| S8 | `p_sequencer` via `` `uvm_declare_p_sequencer `` | EU VS | B | C | member `p_sequencer` with no `macro_of` |
| S9 | Items are generated only by sequences | EU | A | C | `start_item`/`finish_item` outside `is_sequence` |
| S10 | Virtual sequences start on null or a virtual sequencer; no items from a vseq | LR EU VS | A | P | argument type of `start`; `_vseq` role by name |
| S11 | Objections in a sequence only in `pre_start`/`post_start` | VS EU | A | P | CV forbids `starting_phase` use entirely |
| S12 | `starting_phase` is null-checked before raising | EU VS | B | P | `before()`/enclosing `if` on `!= null` |
| S13 | No `fork … join_any; disable fork` of child sequences | MG | A | C | `siblings` of the fork |
| S14 | Sequences extend `uvm_sequence #(REQ)` with the item type | EU | A | C | `type_arg` is an item, not the default |
| S15 | No sequence-library macros / `count` | SF AE CC | B | C | `MacroUse` |
| S16 | Virtual sequencer handles are null-checked before use | MG | D | | path-sensitive |

### 3.5 Phasing, objections and end of test

| # | Rule | Sources | Tier | | How |
|---|---|---|---|---|---|
| P1 | An overridden phase method calls `super` when the parent is user code | EU LR VL SG VS DU | A | C | §4 UVM030 |
| P2 | No `super.build_phase` when extending a UVM base directly (EU) / do call it (VL, Cooper) | EU SF ED VL VS | A | P | same shape, inverted, as an option |
| P3 | `super.X()` matches the enclosing method | DU | A | C | |
| P4 | Drivers/monitors/scoreboards implement only `run_phase` | EU MG SF VS | A | C | |
| P5 | No runtime sub-phases (MG) / only in test and env (EU); never `pre_*`/`post_*` | MG SF ED EU VS | A | P | |
| P6 | Don't mix `run_phase` and sub-phases in one component | VS | A | C | |
| P7 | No phase jumping, domains or user-defined phases | MG SF ED | A | P | EU allows user phases |
| P8 | `.connect()` only in `connect_phase` | EU CV | A | C | |
| P9 | config_db get/set only in `build_phase` (or constructor) | EU VS | A | C | |
| P10 | `raise`/`drop_objection` pass a description | EU LR VS | A | C | argument count |
| P11 | No objections in loops or in drivers/monitors (per transaction) | MG EU ED SF VS | A | P | LR allows a monitor's `phase_ready_to_end` |
| P12 | Objections only in tests (MG) / default sequences (CV) | MG CV LR | A | P | role of the enclosing class; option |
| P13 | `run_test()` without an argument | LR SF VS | A | C | |
| P14 | No `$finish`/`$stop` in classes; nothing after `run_test()` | MG CC SF | A | C | `siblings` after the call |
| P15 | No automatic phase objection | VL | A | C | |
| P16 | Raise before the first time-consuming statement | SF | B | C | `before()` |
| P17 | Every raise is dropped on every path | VS (implicit) | D | | path-sensitive; Tier B can only count per method |
| P18 | Timeout set (in the base test / before `run_test`) | LR CC | C | | `all_of(Call)` exists `set_timeout` |
| P19 | `set_propagate_mode(0)` | EU VS | C | | |

### 3.6 Configuration

| # | Rule | Sources | Tier | | How |
|---|---|---|---|---|---|
| G1 | No `uvm_resource_db`, no `set/get_config_*` | EU MG LR VS | A | P | CV uses resource_db for vifs |
| G2 | `config_db::get` result checked | EU LR VS (Cooper) | A | C | §4 UVM040 |
| G3 | No wildcard or regex in the field name; no absolute paths; no whitespace; valid regex | LR ED SP EU VS | A | C | `const_value` + `matches`; SP = Spear |
| G4 | config_db carries config objects or vifs, not ints/strings | MG LR ED SP VL EU | A | P | `type_arg(owner(get), 0)`; allowed types as an option |
| G5 | Context argument is `this` in components; `null` only from modules | CV VS | A | C | |
| G6 | The vif is set from the top module with scope `"uvm_test_top"` | MG EU VS | A | C | enclosing `Module` + `const_value` |
| G7 | No config_db `get` in run-time loops | MG ED | A | C | `ancestors` loop ∨ enclosing `run_phase` |
| G8 | config_db `set` only in tests and virtual sequences | VS | A | | |
| G9 | Config classes extend `uvm_object`; registered or not (P) | EU VL SF | A | P | |
| G10 | Every `get` has a matching `set` (same field, compatible type) | VS (implicit) | C | C | model scope; constant keys only; silent if either side is computed |
| G11 | `set`/`get` type parameters match exactly | VL | C | C | same index as G10 |
| G12 | Don't `set` a null handle | VL | B | C | `before()` holds an assignment to it |
| G13 | Configs form a tree; a component gets only its own | EU SF ED VL LR | D | | run-time hierarchy |

### 3.7 Components, agents and TLM

| # | Rule | Sources | Tier | | How |
|---|---|---|---|---|---|
| A1 | An agent contains only sequencer, driver, monitor (+ config) | EU LR VS | A | P | `members(c)` component types |
| A2 | Driver/sequencer built only under `is_active` | EU LR SF | A | C | `create` has an `ancestors` `if` whose condition mentions `is_active`/`get_is_active`; approximate |
| A3 | Driver: one sequencer, one vif, no analysis ports | LR VS | A | C | |
| A4 | A monitor never assigns interface signals | EU SF VS | A | C | §4 UVM050; callees are Tier D |
| A5 | Signals are driven only in drivers; vifs used only in drivers/monitors | VS | A | | |
| A6 | Class signal access through clocking blocks; NBA to clocking signals | EU LR VS | A | P | access path includes a `ClockingBlock` member |
| A7 | Only drivers/monitors consume time | EU VS | A | C | |
| A8 | No handles between driver/monitor/scoreboard; no `uvm_top.find` | EU CV VS | A | C | member types by role |
| A9 | Multiple analysis inputs use `` `uvm_analysis_imp_decl `` | EU AE CC VS | A | C | >1 member of `uvm_analysis_imp` itself |
| A10 | Scoreboard buffers in `uvm_tlm_analysis_fifo` | LR CC | A | P | |
| A11 | No UVM callbacks | MG SF | A | P | EU allows `pre_do` etc. |
| A12 | No `$display`/`$write` in UVM classes | EU MG AE LR SG DU VS | A | C | system call inside a `uvm_object` subclass |
| A13 | Class name suffix follows its role (base class) | CV DU VS SL | A | P | suffix map is an option |
| A14 | No `uvm_comparator` family (removed in 1800.2) | CC | A | C | part of K14 |
| A15 | All TLM ports connected | VS | C | | model-scope `connect` calls by member; approximate (arrays, run-time) |
| A16 | One agent per DUT interface | EU LR | D | | |

### 3.8 Messaging

| # | Rule | Sources | Tier | | How |
|---|---|---|---|---|---|
| M1 | No direct `uvm_report_*` calls (use the macros) | EU MG LR | B | C | call ∧ `!macro_of(node).hasValue()` |
| M2 | Report ID is a constant or `get_type_name()` / `` `gfn `` | EU LR VS | A | P | argument of the expanded `uvm_report_enabled` |
| M3 | Verbosity: no `UVM_NONE`/`UVM_FULL`; enum, not int | LR VS DU | A | P | expanded argument; allowed set is an option |
| M4 | No `` `uvm_warning `` | LR VB | B | P | `MacroUse`; MG allows it |
| M5 | No `get_full_name()`/`%m` in messages | VS | A | | |
| M6 | No report macros in `final`; no `fatal` in build; no error before start of simulation | VS | A | | |
| M7 | Format-string mismatches | DU VB SL | A | C | map slang `format-*` warnings |
| M8 | No `;` after a `` `uvm_* `` macro | LR CV VB | B | C | `EmptyStatement`/`EmptyMember` immediately after a `MacroUse` span |
| M9 | No `uvm_object::print()`; one-line logs | LR CV | A | C | |

### 3.9 Coverage

| # | Rule | Sources | Tier | | How |
|---|---|---|---|---|---|
| V1 | Covergroups not in items or sequences | EU MG VS | A | P | MG puts them in wrapper classes |
| V2 | Covergroup constructed in the constructor | EU MG VS | A | C | |
| V3 | Sampled with `sample()`, not a clocking event | EU MG VS | A | C | |
| V4 | No `option.weight`/`option.goal`; `per_instance` set | EU VS AF | A | P | |
| V5 | `ignore_bins`, not `illegal_bins` | EU LR VS AF | A | C | |
| V6 | Coverpoints and crosses labelled; `_cg` suffix | MG DU AF CV | A | P | |
| V7 | `coverage_enable` knob gates construction | CV EU | A | P | |
| V8 | Large auto-bin counts | MG AF | A | C | `type_of(cp).bitWidth` > option without `bins` |
| V9 | Sample DUT outputs, not stimulus | EU | D | | |

### 3.10 SystemVerilog language in testbenches

| # | Rule | Sources | Tier | | How |
|---|---|---|---|---|---|
| L1 | No `program` (also `checker`, `final` per MG) | EU MG LR | A | P | CC 2006 recommended programs |
| L2 | Clocks/resets generated in modules, not classes | EU CC LR | A | C | `forever` toggling a signal inside a class; approximate |
| L3 | No `#0` | MG CC | A | C | |
| L4 | `$cast` result checked | MG AE CV | A | C | as S2 |
| L5 | No side effects inside `assert()` | MG LR | A | C | non-pure call in an immediate assertion |
| L6 | No `[*]` associative arrays | MG LR | A | C | |
| L7 | Forbidden: `$random`, `$psprintf`, `$dist_*`, `$srandom`; `$urandom` for members | LR VB CV | A | C | list is an option |
| L8 | `void'` only on allowed calls; never on `randomize`/`uvm_hdl_read` | LR VB | A | C | |
| L9 | Explicit `static`/`automatic` on package/module functions | LR VB | A | C | |
| L10 | Constraint style: no `foreach`/arithmetic/`%`; no `soft` (CV) | LR CV | A | P | |
| L11 | `dist` and `solve…before` hazards; `.sum()` width | AF VS SL | A | C | `type_of` width; map slang `constraint-*` |
| L12 | Fork hazards: `disable` by label; `join_any`+`disable fork` outside an isolation fork; loop variable in `join_none` | LR VB VS SL VT | A | C | |
| L13 | `wait` without a watchdog | LR | A | P | bare `wait` not under a fork with a timeout branch |
| L14 | No `` `timescale ``; delays carry units | CV VT | B | P | `Directive` kind |
| L15 | No hierarchical DUT references in classes | EU LR | A | C | `HierarchicalReference` inside a class |
| L16 | Plusargs not prefixed `uvm_`; no `$test$plusargs` | MG VB VS | A | C | |
| L17 | `rand` on 4-state types | AF VS | A | | |
| L18 | Static initialisation order | MG | D | | |

### 3.11 Files, packages and preprocessor

| # | Rule | Sources | Tier | | How |
|---|---|---|---|---|---|
| X1 | One class per file; file named after the class | MG LR CV DU VS | A | P | `file_of(c).stem == c.name`; extensions and exemptions are options |
| X2 | Package `*_pkg`, in `<pkg>.sv` | EU MG CV VB | A | C | |
| X3 | Class files included only from the package file; no nested includes; included once | EU MG CV LR | C | C | `includes()` graph |
| X4 | Include guards on class files, not on package files; guard name convention | EU CV DU LR | B | P | `Directive` |
| X5 | `` `include "uvm_macros.svh" `` + `import uvm_pkg::*` | EU SF LR | B | C | |
| X6 | No wildcard import in `$unit` or inside a class | EU MG VS SL | A | C | |
| X7 | No cyclic package dependencies | CV LR | C | C | import graph |
| X8 | Macro hygiene: prefix, parenthesised arguments, `begin`/`end`; local macros `` `undef ``'d | LR | B | C | `Directive` + macro body tokens |
| X9 | `` `endif `` comment names the condition | LR VB | B | C | |
| X10 | `` `define `` not used for constants | CV LR | B | C | |
| X11 | Code in the `$unit` scope | MG | A | C | |

### 3.12 Naming

All Tier A: kind (plus role or type) and a regex option. All need options, because no two
guides agree (§5.2).

| # | Rule | Sources |
|---|---|---|
| N1 | lower_snake_case for classes, variables, methods | EU MG LR CV |
| N2 | Class suffix by role | CV DU VS SL |
| N3 | `m_` prefix (all members / private only / class handles only) | EU MG LR |
| N4 | Member suffix by type (`_port`, `_export`, `_imp`, `_ap`, `_vif`, `_cfg`) | EU MG CV DU LR |
| N5 | Constraint `_c`/`_cnstr`; covergroup `_cg` | LR CV DU VB AF |
| N6 | Enum, parameter and macro case | LR CV EU VB |
| N7 | Standard handle names (`m_driver` vs `driver`; `cfg` vs `m_config`) | EU LR |
| N8 | Package prefix on package-scope names and macros | LR CV |
| N9 | DPI import/export prefixes | LR |
| N10 | Register-block variable `regmodel`; register names upper case | EU UG |
| N11 | End labels match the name | MG VB |

### 3.13 Register layer

| # | Rule | Sources | Tier | | How |
|---|---|---|---|---|---|
| R1 | Register API `status` checked | VS | B | C | `before()`/later reference to the status variable |
| R2 | `update()` after `set()`; no `update()` after `predict()` | VS | B | | `before()` within a method |
| R3 | Register model built in `build_phase`, created if null, `lock_model()` called | EU VS | A | C | |
| R4 | No register handles in scoreboards/monitors | VS | A | | |
| R5 | Backdoor needs an HDL path | VS | D | | elaborated model plus run time |
| R6 | Don't edit generated register models | EU | D | | |

### 3.14 Not lint (Tier D)

| What | Why |
|---|---|
| Formatting: indent width, line length, tabs, trailing space, one statement per line, `begin`/`end` placement, spacing | a formatter's job (`verible-verilog-format`), as pssfmt owns it for PSS |
| File headers / SPDX lines | a header tool's job (`reuse lint`, a CI script) |
| Test only configures (EU) vs test starts sequences (SF, MG) | judgment, and the sources conflict |
| Item legal when randomized; sequence inputs `rand` | intent |
| Driver drives `X` in don't-care cycles | run-time behaviour |
| Emulation split (HDL/HVL tops) | project structure; a separate rule pack at most |

### 3.15 Tally

Counted from the tables above. F18 is folded into F16 and A14 into K14, so they are not counted.

| Section | A | B | C | D | Total |
|---|---:|---:|---:|---:|---:|
| Factory (3.1) | 16 | 1 | 0 | 0 | 17 |
| Class structure (3.2) | 13 | 2 | 1 | 0 | 16 |
| Transactions (3.3) | 8 | 2 | 0 | 3 | 13 |
| Sequences (3.4) | 9 | 6 | 0 | 1 | 16 |
| Phasing (3.5) | 15 | 1 | 2 | 1 | 19 |
| Configuration (3.6) | 9 | 1 | 2 | 1 | 13 |
| Components (3.7) | 13 | 0 | 1 | 1 | 15 |
| Messaging (3.8) | 6 | 3 | 0 | 0 | 9 |
| Coverage (3.9) | 8 | 0 | 0 | 1 | 9 |
| Language (3.10) | 16 | 1 | 0 | 1 | 18 |
| Files (3.11) | 4 | 5 | 2 | 0 | 11 |
| Naming (3.12) | 11 | 0 | 0 | 0 | 11 |
| Register layer (3.13) | 2 | 2 | 0 | 2 | 6 |
| **Total** | **130** | **24** | **8** | **11** | **173** |
| Not lint (3.14) | | | | 6 groups | |

- **Tier A is 75% of the rules, and Tiers A+B together are 89%.**
- 82 Tier A rules carry the consensus mark (C).
- The naming rows are few but each stands for several rules (one per declaration kind), so
  by rule count naming is under-represented here.

**Caveat on "A".** Tier A means *expressible*, not *precise*.
- Several Tier A rules are approximations of a flow property, and need an option or a waiver
  for the cases they miss or over-report:
  - F16 "create in `build_phase`" misses helper methods called from `build_phase`.
  - A4 "monitor never drives" misses a call into a helper class.
  - A2 "built under `is_active`" uses a syntactic guard.
- This is the same trade-off the PSS rules make, and the `nursery` tier exists for it.

> **Update (2026-10-15):** [`sv-uvm-elaboration-checks.md`](sv-uvm-elaboration-checks.md)
> proposes building the run-0 UVM elaboration (component tree, config_db, overrides, TLM) as a
> derived model that rules query. With it, 11 of the 19 Tier C/D rows above become precise, and
> about 10 new rules for UVM's run-time-only failures become possible. sphinx-systemverilog
> already extracts the type-level half of that model.

## 4. Worked examples

Written against slang's semantic AST. Kind and field names follow slang's C++ (`ClassType`,
`Subroutine`, `CallExpression`, `NewClassExpression`, `AssignmentExpression`); in a real host
they come from the generated schema. The file assumes `uses: [uvm.yaml]` (§2.1) for `is`,
`cls`, `is_driver`, `PHASES` and so on.

```yaml
sv-rules:
  version: 1
  uses: [uvm.yaml]            # Q-F2: shared UVM vocabulary (§2.1)

  defaults:
    tier: suspicious
    since: "2026.1"

  define:
    factory_create(c): >-
      c.isa(CallExpression) && c.subroutine.name == "create" &&
      (is(owner(c.subroutine), "uvm_component_registry") ||
       is(owner(c.subroutine), "uvm_object_registry"))
    config_db(c, m): >-
      c.isa(CallExpression) && c.subroutine.name == m && is(owner(c.subroutine), "uvm_config_db")
    discarded(c): >-
      parent(c).isa(ExpressionStatement) ||
      (parent(c).isa(ConversionExpression) && type_of(parent(c)).isVoid)

  rules:
    - id: UVM001
      name: unregistered-uvm-class
      family: factory
      summary: A UVM object or component class is not registered with the factory
      message: >-
        '${node.name}' is a ${is(node, "uvm_component") ? "uvm_component" : "uvm_object"}
        but is not registered; type_id::create and factory overrides cannot see it
      doc: |
        Add `uvm_component_utils(T) or `uvm_object_utils(T) (`_param_utils for a
        parameterised class). The rule looks for the `type_id` the macros declare, so a
        hand-written registration or a house wrapper macro also satisfies it.
      options:
        exempt_bases:
          type: string-list
          default: []
          help: Classes whose subclasses need not be registered (e.g. a config base, per Easier UVM)
      match: { kind: ClassType }
      where: >-
        !in_library(node) && !node.isAbstract && is(node, "uvm_object") &&
        member(node, "type_id") == null &&
        !options.exempt_bases.exists(b, derives_from(node, b))
      examples:
        - code: |
            import uvm_pkg::*; `include "uvm_macros.svh"
            class my_item extends uvm_sequence_item;   // expect: UVM001
              function new(string name = ""); super.new(name); endfunction
            endclass
        - code: |
            import uvm_pkg::*; `include "uvm_macros.svh"
            class my_item extends uvm_sequence_item;
              `uvm_object_utils(my_item)
              function new(string name = ""); super.new(name); endfunction
            endclass

    - id: UVM002
      name: registration-flavour-mismatch
      family: factory
      summary: A class is registered with the wrong macro flavour or for another class
      message: >-
        '${node.name}' is registered as ${type_arg(type_of(member(node, "type_id")), 0).name}
        through ${type_of(member(node, "type_id")).name}; use
        `uvm_${is(node, "uvm_component") ? "component" : "object"}_utils(${node.name})
      match: { kind: ClassType }
      let:
        reg: type_of(member(node, "type_id"))
      where: >-
        !in_library(node) && member(node, "type_id") != null &&
        (type_arg(reg, 0) != node ||
         is(node, "uvm_component") != is(reg, "uvm_component_registry"))

    - id: UVM010
      name: uvm-object-new
      family: factory
      summary: A UVM object or component is constructed with new() instead of the factory
      message: >-
        ${type_of(node).name} is constructed with new(); use
        ${type_of(node).name}::type_id::create(...) so factory overrides apply
      match: { kind: NewClassExpression }
      where: >-
        !in_library(node) && is(type_of(node), "uvm_object") &&
        !is(type_of(node), "uvm_port_base") && !in_method(node, ["create"])

    - id: UVM011
      name: create-name-mismatch
      family: factory
      tier: style
      summary: The name passed to create() differs from the variable it is assigned to
      message: >-
        '${node.left.symbol.name}' is created with the name
        "${const_value(node.right.arguments[0]).value()}"; reports and config_db paths
        will show a different name
      match: { kind: AssignmentExpression }
      where: >-
        factory_create(node.right) && node.left.isa(NamedValueExpression) &&
        const_value(node.right.arguments[0]).hasValue() &&
        const_value(node.right.arguments[0]).value() != node.left.symbol.name

    - id: UVM020
      name: unchecked-randomize
      family: stimulus
      summary: The result of randomize() is ignored
      message: >-
        randomize() ${discarded(node) ? "result is discarded" : "is inside an assertion"};
        a constraint failure goes unnoticed${ancestors(node).exists(a,
        a.isa(ImmediateAssertionStatement)) ? " and disappears when assertions are off" : ""}
      match: { kind: CallExpression }
      where: >-
        node.subroutine.name == "randomize" && !in_library(node) &&
        (discarded(node) || ancestors(node).exists(a, a.isa(ImmediateAssertionStatement)))

    - id: UVM030
      name: phase-missing-super
      family: phasing
      summary: An overridden phase method does not call the parent's implementation
      message: >-
        ${node.name} overrides ${owner(overrides(node).value()).name}::${node.name}
        but never calls super.${node.name}(); the parent's ${node.name} is skipped
      match: { kind: Subroutine }
      where: >-
        node.name in PHASES && is(cls(node), "uvm_component") &&
        overrides(node).hasValue() && !in_library(overrides(node).value()) &&
        !calls(node).exists(c, is_super_call(c) && c.subroutine.name == node.name)

    - id: UVM040
      name: unchecked-config-db-get
      family: config
      summary: The result of uvm_config_db::get is ignored
      message: >-
        uvm_config_db::get("${const_value(node.arguments[2]).orValue("?")}") result is
        discarded; a missing set leaves the variable at its default silently
      match: { kind: CallExpression }
      where: config_db(node, "get") && discarded(node)

    - id: UVM041
      name: config-db-scalar-type
      family: config
      tier: style
      summary: uvm_config_db carries a scalar instead of a configuration object
      message: >-
        uvm_config_db #(${type_arg(owner(node.subroutine), 0).name}) passes a scalar;
        put it in a configuration object
      options:
        allow:
          type: string-list
          default: []
          help: Type names allowed besides classes and virtual interfaces
      match: { kind: CallExpression }
      let:
        t: type_arg(owner(node.subroutine), 0)
      where: >-
        (config_db(node, "set") || config_db(node, "get")) &&
        !(t.isClass || t.isVirtualInterface) && !(t.name in options.allow)

    - id: UVM050
      name: monitor-drives-interface
      family: architecture
      tier: correctness
      summary: A monitor assigns a signal through a virtual interface
      message: >-
        monitor '${cls(node).name}' assigns ${node.left}; a monitor must only observe
      match: { kind: AssignmentExpression, inside: { kind: ClassType } }
      where: is_monitor(cls(node)) && type_of(access_root(node.left)).isVirtualInterface

    - id: UVM060
      name: no-uvm-do-macros
      family: stimulus
      tier: style
      summary: A `uvm_do-family macro is used
      message: "`${node.name} hides start_item/randomize/finish_item; write them out"
      match: { kind: MacroUse }                 # Tier B: host preprocessor view (§2.2)
      where: node.name.matches("^uvm_(do|send|create|rand_send)")

    - id: UVM080
      name: sequence-started-unrandomized
      family: stimulus
      summary: A sequence is started without being randomized first
      message: >-
        '${node.thisClass}' is started without randomize(); its rand fields keep their
        defaults
      match: { kind: CallExpression }
      let:
        seq: resolves_to(access_root(node.thisClass))
      where: >-
        node.subroutine.name == "start" && is(owner(node.subroutine), "uvm_sequence_base") &&
        seq != null && type_of(seq).hasRandMembers &&
        !before(node).exists(s, calls(s).exists(c, c.subroutine.name == "randomize" &&
                                                   resolves_to(access_root(c.thisClass)) == seq))
```

**What this shows:**
- **Most rules are 3–6 lines of predicate.** The longest `where:` is UVM080, at 4 lines.
  The doc and the examples are most of each rule.
- **Semantic beats syntactic for UVM.** UVM001–003 work without knowing a single macro name.
  UVM041 reads the type argument of a specialisation, which a CST linter (Verible, svlint)
  cannot do.
- **The bounds show where expected.**
  - UVM080 accepts a sequence randomized in only one branch of an earlier `if`, because
    `before()` is straight-line.
  - UVM050 does not follow calls into helper functions.
  - Both are documented limits, not silent ones.

## 5. Quickly building a well-structured-UVM rule set

### 5.1 A consensus starter pack (36 rules)

These 36 rules are Tier A and supported by at least three sources or two tools. All are
marked **C** except F1, where the only disagreement is which classes are exempt, so it takes an
option. This is the pack to write first:

| Area | Rules |
|---|---|
| Factory | F1 (exempt option), F2, F3, F5, F7, F9, F11, F13, F16 |
| Construction | K1, K2, K4, K14 (version option) |
| Transactions | T1, T3 (list option), T8 |
| Sequences | S2, S5, S6, S9 |
| Phasing | P1, P3, P4, P8, P10, P13, P14 |
| Config | G2, G3, G6 |
| Components | A4, A7, A12 |
| Language | L3, L7, L12 |

Writing them is mostly mechanical once the helpers file exists: the `where:` clauses above are
typical. With `examples:` (one firing, one near-miss) as the test suite, a rule is about
30–40 lines including documentation. **The pack is roughly 1,200 lines of YAML.** The
lint-candidates harvest workflow (`lint-candidates/README.md`, 242 PSS cards) is the process
model: one card per catalogue row, then a rule.

### 5.2 Profiles for where the guides disagree

Each conflict becomes an option on one rule, plus named **presets** that set them. A preset is
just a config fragment: the existing `[checker.<rule>]` option mechanism plus selection, so no
new kernel concept is needed. The conflicts found:

| Topic | Easier UVM | Mentor cookbook | lowRISC / OpenTitan | CORE-V-VERIF | Rule(s) |
|---|---|---|---|---|---|
| Field macros | ban | ban | items only, never components | component config fields | T2 |
| `super.build_phase` when extending a UVM base | don't | — | when the parent is user code | — | P1, P2 (Verilab: always) |
| Runtime sub-phases | test/env only; no `pre_`/`post_` | avoid | — | — | P5 |
| User-defined phases | allowed | avoid | — | — | P7 |
| Objection owner | test, with description | test only | test; monitor `phase_ready_to_end` | default sequences; no `starting_phase` | P11, P12, S11 |
| `` `uvm_warning `` | — | allowed | banned | — | M4 |
| Verbosity | default high | — | no `UVM_NONE`/`UVM_FULL` | — | M3 |
| Registration macro position | first line | — | after members | full section order | K6, K7 |
| Register config classes | no | — | yes (all classes) | — | F1, G9 |
| `virtual` on overrides | omit | — | — | required | K9 |
| `extern` methods | — | yes | — | no prototypes | K12 |
| resource_db | ban | ban | ban | for vifs | G1 |
| Covergroup placement | components | wrapper classes | — | — | V1 |
| Enum value case | UPPER | — | UpperCamel | — | N6 |
| Suffixes | `_config`, `_vif`, `_pkg` | `_h`, `_ap`, `_cfg` | `_if`, `_c` (constraint) | `_c` (class), `_intf`, `_cnstr` | N2–N5 |
| Driver item pull | `try_next_item` | — | — | — | (SF: `get_next_item`) |

Recommended presets: `uvm-consensus` (5.1 only), `easier-uvm`, `lowrisc-dv` and `core-v`.
The two named projects (lowRISC/OpenTitan and CORE-V-VERIF) are also the obvious validation
corpora.

### 5.3 Validating a pack before trusting it

1. **Local corpora first.** `~/projects/uvm-tb-proto/out` (LLM-generated testbench, 23 files)
   and `featherweight-ip/fw-wb-dma/tests/uvm` (hand-written). The generated tree is the most
   interesting one: it is exactly the agent-guardrail case in `sv-lint-demand.md`, and
   pssparser `docs/design/llm-guidance-from-rules.md` proposes the loop.
2. **OpenTitan `hw/ip/*/dv`** with the `lowrisc-dv` preset. It should be nearly clean;
   every finding is either a rule bug or a real deviation, and both are informative.
3. **CORE-V-VERIF** with `core-v`, and the same tree with `easier-uvm` to measure how much
   the presets disagree in practice.
4. **Precision bar:** the lint-criteria rules apply unchanged. A rule that cannot reach the
   `suspicious` tier's precision on these corpora goes to `nursery` or `pedantic`.

## 6. Host risks specific to UVM (extending survey §4)

| Risk | Why UVM makes it worse | Spike question |
|---|---|---|
| **S-2 generic class bodies** | Most UVM components are parameterised (`uvm_driver #(REQ)`, user `my_agent #(type CFG)`). Slang builds a `ClassType` per specialisation; an uninstantiated generic class (common in a VIP package) may never be elaborated. | Can the host run class rules on each generic body exactly once (with `T` unresolved) and fall back to the specialisations? Do UVM001–050 behave on `fwvip-*` packages compiled without a test? |
| **S-3 macros** | Registration, messages, sequences and field automation are all macros (§2.2); `uvm_pkg` macro bodies would otherwise produce findings at `uvm_macros.svh` lines. | `MacroUse` + `macro_of` from slang's `SourceManager`; report-at-use-site policy |
| **Compiling `uvm_pkg`** | Every Tier A rule needs `uvm_pkg` elaborated, so `derives_from(..., "uvm_pkg::uvm_driver")` resolves. Verible-style per-file lint is impossible here. | Time and memory to compile UVM 1.2 / 2020 with slang; cache the elaborated `uvm_pkg` across runs (the LSP needs this) |
| **S-4 project context** | A UVM testbench needs the file list, `+incdir`, defines and the UVM version. | Read the file list from `flow.yaml`/FuseSoC/`.f` files; Q-SV4 (degraded mode) matters more here than for RTL |
| **Library vs user code** | `in_library` must exclude `uvm_pkg` and vendor VIP, or rules fire inside them. | A `[lint] library = [...]` glob, as for PSS's standard library |

None of these is about the rule format. All of them are in the survey's §5.3 spike, which this
report would amend: **swap its "4 Easier-UVM/cookbook rules" for the ten rules of §4**. They
cover all three Tier A shapes (structure, strings, type arguments) and both Tier B functions.

## 7. Recommendations

1. **Treat the catalogue (§3) as the requirements list** for the SV host's function library.
   The generic library plus `MacroUse`/`macro_of`, `before`, the include/directive view and
   `access_root` covers Tiers A and B. No UVM-specific C++ is needed.
2. **Pull Q-F2 (`uses:`) forward** for the SV format: the UVM vocabulary file is shared by
   every rule.
3. **Write the consensus pack (§5.1) first**, and the presets (§5.2) second. Do not encode one
   guide wholesale: every guide has positions that others reject.
4. **Run the §4 rules in the spike** against the two local corpora before anything else. That
   answers S-2 and S-3 with real code.
5. **Leave Tier C to a later version** unless the spike shows config_db set/get mismatch
   (G10/G11) is cheap. It is the highest-value cross-file rule: the debugging cost is
   well documented (Cooper & Marriott, Edelman), and no open tool checks it.

## 8. Open questions

- **Q-UVM1.** Ship the role vocabulary as a YAML helpers file (recommended, §2.1) or as host
  functions?
- **Q-UVM2.** Should `MacroUse` be a node kind visible to `match:`, or only reachable through
  `macro_of()`? A kind is simpler for rule authors. A function keeps the semantic tree clean.
- **Q-UVM3.** Are presets shipped with the pack (`--preset lowrisc-dv`), or left as config
  examples in the docs?
- **Q-UVM4.** Should slang's own warnings that overlap (K10, L11, L12, M7) be mapped into the
  UVM pack's selection, or stay in a separate `SLANG` namespace? The survey (§3 caveat)
  assumed the latter.
- **Q-UVM5.** Naming: the earlier draft used `FWH`/`LINT`-style IDs. A UVM pack needs its own
  namespace. `UVM` is the IEEE 1800.2 name, so under the rule that ID prefixes avoid EDA
  language and standard names (the reason `PSL` was rejected for lint), it should probably be avoided (e.g. `UVMC` for "UVM check"). The IDs in §4 are illustrative.
