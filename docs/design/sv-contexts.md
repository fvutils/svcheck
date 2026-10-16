# svcheck: contexts

| | |
|---|---|
| **Status** | Proposed, 2026-10-16. Q-CX1–Q-CX3 decided by the user the same day (§7); Q-CX4 and Q-CX5 open; Q-CX6 deferred. **Exploratory:** no SV tool we found does this, so every decision here is provisional and expected to change after trials on real testbenches (§6.1). |
| **Home** | `svcheck/docs/design/`, a companion of `svcheck-design.md` (§3.7 there points here). |
| **What** | A SystemVerilog verification environment is made of several **contexts**: the UVM env, each agent (VIP), the test layer, the testbench top, the HDL BFMs, the DUT and the library code. Which rules apply, and with which options, depends on the context. This document says what a context is, how svcheck finds one, and what that changes in the checker and in the rules. |

## 0. Summary

- **Most languages have one context.** A Java or Python linter's rules mean the same thing in
  every file, and path globs are enough to scope the exceptions. An SV testbench is different:
  - an **agent** must be self-contained and reusable;
  - an **env** composes agents;
  - a **test** may override and raise objections;
  - an **HDL BFM** must be synthesizable for co-emulation;
  - **`uvm_pkg`** and third-party VIP are read-only.

  A rule that is right in one context is wrong in the next.
- **A context is a set of labels on a *unit*.** A unit is a package, an interface, a module or
  (as a fallback) a class. Context is not a property of a file. UVM code is
  `` `include ``d into packages, so the package is the unit and the file path is weak evidence.
- **Contexts are found from evidence, in three strengths.** In order:
  1. **Declared:** a user tag in the source or in the config.
  2. **Content:** what the code does. For example, a `virtual X_if` fetched from `uvm_config_db`
     means an interface agent.
  3. **Convention:** names and directories, such as `*_agent_pkg`, `uvma_` or `hw/dv/sv/*_agent`.

  The evidence is recorded and can be shown. When the sources disagree, svcheck reports a
  finding; it does not pick one silently.
- **The classifiers are data.** They are CEL predicates in rule packs, under a new `contexts:`
  key. The engine knows labels and units, not UVM. This is the same principle as roles as YAML
  helpers (assessment §2.1).
- **Contexts become things rules can match** (a `Unit` facet). That gives rules four shapes:
  1. **in-context:** "no objections in an agent";
  2. **boundary:** "an agent package must not import an env package";
  3. **unit structure:** "an agent has a config class", "an env lives in a directory named
     `env`", "this package mixes agent and env content";
  4. **classification consistency:** "tagged as an agent but has no driver or monitor".
- **The checker changes in four places:**
  - a classify stage before the rules run;
  - a unit dependency graph from the host;
  - **selection by context**, which generalises the forked engine's per-file groups;
  - findings that carry their context.

  The engine still holds no SV or UVM types (design §3).

## 1. Why SV needs contexts

The same construct is judged differently in different contexts:

| Construct | Agent (VIP) | Env | Test | TB top / HDL | Library |
|---|---|---|---|---|---|
| `phase.raise_objection` | no (MG, CV: only default sequences) | rarely | yes | — | skip |
| `"uvm_test_top..."` string or absolute config scope | no: an agent can't know where it sits | discouraged | yes | yes (vif `set`, G6) | skip |
| Hierarchical reference to DUT signals | no: through the vif only | no | no | yes (bind, probes) | skip |
| `import <x>_env_pkg::*` | **no:** reversed layering | sub-envs only | yes | yes | — |
| Factory override | no | rare | yes (F14) | — | — |
| `#` delay, `wait`, `@(posedge)` | in driver/monitor; **not** in an HVL proxy under co-emulation | no | no | yes (HDL) | — |
| Class-typed task argument in an interface | ok in simulation; **no** in an XRTL BFM | — | — | no in the HDL partition | — |
| Naming suffixes, field macros | per preset | per preset | per preset | — | skip |
| Generated code (RAL, UVMF scaffolding) | style rules off, correctness rules on | | | | |

The methodologies already encode these contexts (research, 2026-10-16):

| Source | How it marks contexts | Rules tied to them |
|---|---|---|
| **Siemens UVMF** | interface packages (`<x>_pkg`, `<x>_pkg_hdl`, `<x>_driver_bfm`, `<x>_monitor_bfm`), env packages (`<blk>_env_pkg`), bench (`tb/{testbench,tests,sequences}`, `hdl_top`/`hvl_top`); separate HDL and HVL filelists | the HDL side is synthesizable; the HVL side has no timing |
| **Co-emulation (Mentor, "Off to the races", DVCon)** | dual top: `hdl_top`, `hvl_top`; BFMs are interfaces named `_bfm` | no cross references between the tops; HVL strictly untimed; no class or unpacked arguments to BFM tasks; HDL-to-HVL calls are functions only; no factory/config for BFMs; covergroups in the HVL proxy |
| **lowRISC OpenTitan** | `dv_lib`, `cip_lib` (base libs), `hw/dv/sv/*_agent`, `hw/ip/<ip>/dv/env`; packages `<x>_agent_pkg`, `<dut>_env_pkg`, `<dut>_test_pkg`; module `tb` in `tb.sv` | env imports agents, test imports env; files included into a package live in its directory; assertions bound from bind files |
| **CORE-V-VERIF / Moore.io** | prefixes `uvma_` (agent), `uvme_` (env), `uvmt_` (testbench), `uvml_` (library) | "a testbench shall not use code from another testbench directory; only vkit code is visible" |
| **Doulos Easier UVM** | names ending `_agent`, `_env`, `_pkg` | one agent per interface; driver and sequencer only when active; an env works as top or as sub-env |
| **Verification Academy** | an agent is a signal interface, driver and monitor BFM interfaces, and a package of classes | config object holds vif and `is_active`; passive builds no driver |

The tools mostly don't. **Verissimo** comes closest. Its architecture model derives a component
type (AGENT, DRIVER, ENVIRONMENT, TEST, …) from a base class plus a name pattern, and it skips
third-party IP by path or by package. **Sigasi** varies a rule's severity per build target
(synthesis vs simulation). Verible, svlint and the commercial RTL linters scope by path only.
Other ecosystems split three ways:
- by **path**: ESLint `files`, clang-tidy, dependency-cruiser;
- by **declared tag**: Nx `tags` + `depConstraints`;
- by a **role derived from path or structure**: eslint-plugin-boundaries, ArchUnit layers
  `definedBy` a package pattern or a supertype, Android Lint's test source sets.

svcheck needs all three, and SV makes the derived kind matter most.

## 2. What a context is

### 2.1 Axes

A context is a set of **labels**, one or more on each of a few independent axes:

| Axis | Labels (UVM pack's vocabulary; illustrative) | Notes |
|---|---|---|
| **Provenance** | `user`, `library` (uvm_pkg, base libs), `vip` (third party), `generated` | generalises `in_library` (S-6). Declared by packs like the others (Q-CX2), unless replacing `in_library` shows the engine must know it |
| **Methodology** | `uvm`, `uvm+dv_lib`, `uvmf`, `plain-sv` | mostly from imports and base classes |
| **Unit kind** | `agent`, `env`, `test`, `tb-top`, `seq-lib`, `regmodel`, `base-lib`, `bind`, `design` | the user's main example; carries a **unit name** (`agent(apb)`) |
| **Partition** | `hvl`, `hdl` | co-emulation; also design vs TB |
| **Target** | `sim`, `emulation`, `formal` | usually project-wide, from config; turns the partition rules on |

The axes are orthogonal. For example, an APB agent's BFM interface might be
`{user, uvm, agent(apb), hdl, emulation}`.

**Role vs context.** A *role* belongs to a class: driver, monitor, sequence. It stays a YAML
helper evaluated inside `where:` (assessment §2.1). A *context* belongs to a unit, which holds
many classes. A driver is a role; the agent it belongs to is a context. Rules use both: "a
driver (role) in an agent with `emulation` target (context) must not use `#`".

### 2.2 Carriers and propagation

A context attaches to a **carrier** and propagates to everything the carrier encloses:

| Carrier | When |
|---|---|
| package | the normal HVL unit. Files `` `include ``d into it take **its** context, not their path's |
| interface, module, program definition | HDL units: signal interfaces, BFMs, tops, bind modules |
| class | fallback, when a package is mixed (§3.4) |
| file | fallback for code outside any package (`$unit`), and for path conventions |
| component **instance** (level 2) | dynamic context: this agent instance is passive; it sits in env `soc_env` (§5.5) |

A **unit** is one carrier, or several carriers grouped under one unit name. An agent unit is
`apb_agent_pkg` plus `apb_if` plus `apb_driver_bfm`. Grouping comes from evidence too: the
package's config class holds `virtual apb_if`.

## 3. Finding a context: evidence

### 3.1 Three sources

| Strength | Source | Examples |
|---|---|---|
| **Declared** (strongest) | a source tag on the carrier | `// svcheck context: agent(apb)` before `package`, or `(* svcheck_context = "agent" *)` on a package, module or interface (SV attributes are legal there, not on classes) |
| | config | `[[svcheck.contexts]] packages = ["apb_*_pkg"]  context = "agent"` |
| | a VIP's manifest | a library-carried pack (Q-SV5) declares its own units as `vip` + `agent` |
| **Content** | classifier predicates over what the unit contains or does | a class derives from `uvm_agent`, `uvm_env`, `uvm_test`, `uvm_reg_block`; **a `uvm_config_db #(virtual X_if)::get` → an interface agent for `X_if`** (user, 2026-10-16); an interface with tasks, called through a virtual interface from a driver → BFM; a module calling `run_test` → `tb-top`; a package importing agent packages and holding a `uvm_env` subclass → env |
| **Convention** (weakest) | names and paths, supplied by presets | `*_agent_pkg`, `uvma_*`, `uvme_*`, `*_bfm`, `hdl_top`, `hw/dv/sv/*_agent/` |

### 3.2 Resolution

- **Declared wins.** A tag is the user's explicit answer and it is never overridden.
- **Content outranks convention.** Content is what the code does; a name is only what someone
  called it.
- **Every assignment keeps its evidence**: which classifier fired, on which construct, at which
  location. `svcheck --contexts` prints the units, their labels and that evidence. Without this,
  a misclassification surfaces as a confusing finding three steps away.
- **Disagreement is a finding**, not a tie-break:
  - "tagged `agent` but contains a `uvm_env`";
  - "named `*_agent_pkg` but contains no agent".

  Some style guides make the convention mandatory. For them the disagreement *is* the rule
  (§4.3).
- **Unknown is a real answer.** A unit with no evidence on an axis stays unknown there. A rule
  that needs that axis is reported **not run** for the unit. This is the same policy as
  design §3.3: never run on guessed facts.

**Weights: ordered strengths, not numeric scores.** Each classifier is `strong` or `weak`.
Two strong classifiers that disagree on one axis make a conflict, reported as above. A score
would be harder to explain than it is worth.

### 3.3 Classifiers are data

A new top-level key, `contexts:`, in `sv-rules` files (a divergence-log row). A sketch:

```yaml
sv-rules:
  version: 1
  uses: [uvm.yaml]
  contexts:
    - label: agent
      axis: unit
      carrier: Package
      strength: strong
      name: ${cls.name.replace("_agent", "")}     # unit name
      when: exists(cls in classes(carrier), is(cls, "uvm_agent"))
    - label: agent
      axis: unit
      carrier: Package
      strength: strong
      # a virtual interface fetched from config_db: an interface agent for that interface
      when: >-
        exists(c in calls_in(carrier),
               c.subroutine.name == "get" && is_config_db(owner(c.subroutine)) &&
               type_of(type_arg(owner(c.subroutine), 0)).isVirtualInterface)
      joins: type_arg(owner(c.subroutine), 0).interface   # the interface joins the unit
    - label: agent
      axis: unit
      carrier: Package
      strength: weak
      when: carrier.name.matches(options.agent_pkg_pattern)  # preset: "_agent_pkg$", "^uvma_"
```

- **Stratified.** Classifiers run axis by axis: provenance, then methodology, then unit kind,
  then partition. A classifier may read the contexts of earlier axes, never its own, so no
  cycles are possible.
- **Presets carry classifiers.** `lowrisc-dv` brings `_agent_pkg`, `dv_base_*` and `cip_base_*`;
  `core-v` brings `uvma_`/`uvme_`/`uvmt_`/`uvml_`; `uvmf` brings `_pkg_hdl`, `_bfm` and
  `hdl_top`/`hvl_top`. A preset is now classifiers plus options, still a config fragment.
- **A team's base-class library** (`my_base_agent`) is one more classifier line, as roles
  already are.

### 3.4 Mixed units

LLM-generated testbenches, and small hand-written ones, often put everything in one `tb_pkg`.
**A carrier's context is the sum of its contents' (Q-CX3, user, 2026-10-16).** A package
holding an agent class and an env class is `{agent, env}`:
- the package's labels are the union of the labels its contents imply. `mixed` is not a
  separate label: a rule sees two unit kinds on one carrier;
- the classes keep their own labels, so an in-context rule is decided by the **innermost
  carrier that has a label on the rule's axis**. "No objections in an agent" therefore checks
  the agent class in `tb_pkg` and not the env class beside it. Without this, every agent
  rule would also fire on the env class (open: §7, under Q-CX3);
- "more than one unit kind on this carrier" is a unit-structure rule (§4.3), which a
  reuse-minded preset turns on.

## 4. What changes for rules

### 4.1 In-context rules

A rule states where it applies:

```yaml
- id: SVCHK1xx
  name: objection-in-agent
  applies-to: { unit: agent }          # default; config can widen or narrow it
  match: { kind: Call }
  where: node.subroutine.name in ["raise_objection", "drop_objection"]
```

- `applies-to` is **selection**, not part of the predicate. That lets the runner skip the rule
  outside agents at no cost. It also lets config change where it applies without editing the
  rule (§5.3), and lets `--explain` say where it runs.
- Inside `where:`, `context(node)` gives the unit and its labels, for rules that branch on them.
- **Options per context:** a naming rule's suffix map can differ in an agent and in an env.

### 4.2 Boundary rules

Many context rules are really about **dependencies between units**:
- an agent must not import an env or test package;
- an HVL class must not reference an HDL module hierarchically;
- one testbench must not use another's code (CORE-V);
- env → agent → base-lib → library, in that direction only.

These need a dependency graph between units and a rule that matches an **edge**:

```yaml
- name: agent-depends-upward
  match: { kind: UnitDep }
  where: node.from.is("agent") && (node.to.is("env") || node.to.is("test"))
  message: "agent ${node.from.name} depends on ${node.to.kind} ${node.to.name} (${node.via})"
```

Edges come from the host:
- package imports;
- type references across packages;
- hierarchical references;
- `virtual X_if` types;
- `include`s;
- at level 1, config_db scopes and factory names.

A layered-architecture helper, in the style of ArchUnit's `layeredArchitecture()`, is a few
lines of YAML over `UnitDep`.

**Library-carried packs (Q-SV5) fit here.** A VIP's pack mostly checks how others use the VIP,
so its rules are boundary rules on edges *into* the VIP's units, evaluated in the consumer's
context.

### 4.3 Unit-structure rules

The unit is a match target (`kind: Unit`) with fields:
- name;
- labels;
- carriers;
- files and directories;
- package name;
- evidence;
- dependencies.

That covers what path globs can't express:
- **mixed contexts:** "more than one unit kind in this package / file / directory" (user,
  2026-10-16);
- **placement:** "an env not under a directory named `env`" is forbidden (user, 2026-10-16);
  OpenTitan's "files included into a package live in its directory";
- **naming:** "an agent package is named `*_agent_pkg`" (lowRISC) or `uvma_*` (CORE-V);
- **completeness:**
  - an agent has a config class holding its vif, a driver and a monitor (A1);
  - a passive agent builds no driver;
  - one agent per interface (A16, with the unit grouping).

The order is what matters. The context is found from **content and tags**, and the
style rule then checks it against the **path and name**. If the classifier were the path, a
placement rule could never fire.

### 4.4 Classification consistency

These rules are on by default:
- a declared tag contradicted by strong content;
- a convention contradicted by content;
- in a UVM package, a unit that no classifier recognises.

They are cheap, and they find misplaced code and stale tags.

### 4.5 Rule packs

The UVM pack splits along unit kinds:
- `uvm-common`;
- `uvm-agent`;
- `uvm-env`;
- `uvm-test`;
- `uvm-seq`;
- `uvm-regmodel`.

Next to it come `coemu` (the HDL/HVL partition rules, active when `target` includes
`emulation`) and, later, `rtl`. The 36-rule consensus pack is mostly unchanged. About ten of
its rules gain an `applies-to` they currently fake with a role helper or don't have:
- F14: overrides only in tests;
- P12: objections;
- G5 and G6: config scope;
- A8: no handles between driver, monitor and scoreboard;
- K13: `uvm_top`.

## 5. What changes in the checker

### 5.1 A classify stage

```
parse → elaborate (slang) → lower (curated model, §3.6) → classify → facets (level 1/2) → rules
```

Classify runs the pack's `contexts:` classifiers, axis by axis, over the carriers. It produces
the **`Unit` facet**: units, labels, evidence and `UnitDep` edges. Like any facet, it is built
only when a selected rule needs it. Unit classification needs `compilation` scope: base
classes must resolve. In `file` scope only convention evidence exists, which is weak, so
rules that need a unit kind are not run, as design §3.3 requires.

### 5.2 Host additions

The engine still knows only "labels on carriers". It needs three things from the host:
- **carriers:** which nodes are packages, interfaces, modules and classes, and each node's
  enclosing carrier;
- **include provenance:** which package a file was included into. slang knows this through
  the buffer's include location;
- **dependency edges** (§4.2): from slang's resolved references; config_db and factory
  edges come from level 1.

Context tags ride on the waiver path: `collectPragmas` already reads the host's comment
tokens, and `// svcheck context:` is a second pragma form. Attributes come from slang's
`AttributeSymbol`s.

### 5.3 Selection by context

This is the forked engine's main change. Today, selection resolves a **group per file**
(`pipeline/Selection.h`, `groupOf(path)`), and the runner looks up a rule set and options per
file ID (`run/Runner.h`, `RunOptions::perFile`). For SV:
- **a group is per carrier, keyed by the carrier's labels and its path.** There are few
  distinct label sets, so the effective rule sets are resolved once per set and cached, as
  today.
- **The runner keeps the current carrier as it walks.** A rule runs, and a finding is kept,
  according to the carrier's group, not the file's.
- **Config gains context overrides** next to path overrides, merged in the same order
  (ESLint-style):

  ```toml
  [[svcheck.overrides]]
  context = "agent"           # or "agent(apb)", "vip", "hdl"
  select  = ["family:emulation"]
  [svcheck.overrides.rule.class-suffix]
  suffixes = { driver = "_drv" }
  ```

- **`applies-to:`** gives a rule's default set of contexts. Config can widen or narrow it.

### 5.4 Findings

A finding carries its unit and labels:
- in JSON and in SARIF (`properties.context`);
- in the text message where it helps ("in agent `apb`");
- in waivers, which can be scoped to a context ("waive SVCHK1xx in vip").

### 5.5 Level 2: instance contexts

The run-0 interpreter gives contexts that no static view has:
- this `apb_agent` instance is **passive**;
- it sits under `soc_env`;
- it was configured from the test.

These attach to `UvmInstance` facet nodes, not to declarations. Rules such as "a passive agent
built a driver" or "two active agents on one interface" (design §3.4) are matched there. A
declaration used in several instance contexts is reported once per location, with the
instances named (S-1).

### 5.6 The dependency rule still holds

| Layer | Knows |
|---|---|
| engine | labels, carriers, groups, `applies-to`, `Unit`/`UnitDep` as schema kinds; no SV, no UVM |
| SV host | which slang nodes are carriers; include provenance; reference edges; pragma and attribute tags |
| pack (YAML) | the vocabulary (agent, env, …) and the classifiers |
| elab | instance contexts as facet fields |

## 6. Effect on the plan

The schedule is in `svcheck-plan.md` §2.1, as recommendation R-6 (proposed 2026-10-16). In short:
- **P1.3** adds the carrier fields to the curated model;
- **P1.4–P1.5** add `contexts:`, `context(n)`, `applies-to` and `--contexts`;
- a timeboxed trial, **P1.6b**, runs three context rules on the corpora;
- **P2** adds selection by context, presets with classifiers and level-1 edges;
- **P3** adds instance contexts.

### 6.1 Working by trial

This is new ground. Verissimo's component types are the nearest precedent, and they are
roles, not contexts. So the design is expected to change as it meets real code:
- **Try before fixing.** Each open question is settled by running candidate rules on the
  corpora (`uvm-tb-proto/out`, `fw-wb-dma`, one OpenTitan IP, later CORE-V-VERIF), not on
  paper. Where two answers are plausible, try both.
- **Keep it cheap to change.** Until the trials settle it, `contexts:`, `applies-to` and the
  `Unit` fields are marked experimental in the format docs and the divergence log, and no
  built-in rule depends on more than it has to.
- **Record what each trial taught**, as findings in the plan (in the P1-n style), with the
  question it moved.

## 7. Open questions

**Decided (user, 2026-10-16):**
- **Q-CX1. Tag syntax:** both a comment pragma (`// svcheck context: agent(apb)`) and an SV
  attribute (`(* svcheck_context = "agent" *)`), for now. The pragma works everywhere and
  matches waivers; the attribute is structured, but SV allows none on classes.
- **Q-CX2. Vocabulary:** packs declare it (the labels and their axes), unless that turns out
  to be unworkable. The engine knows labels only. Provenance is the axis to watch: replacing
  `in_library` may need the engine to know it.
- **Q-CX3. Mixed carriers:** a carrier's context is the **sum of its contents'** (§3.4).
  In-context rules are decided by the innermost labelled carrier, so that an agent rule
  does not fire on an env class in the same package (confirmed by the user, 2026-10-16).

**Open:**
- **Q-CX4. Evidence: how much is enough, and how it combines.** The sub-questions:
  - Is evidence **ordered** (declared > content > convention, as §3.2 says), or weighed?
  - Does **convention alone** turn on in-context rules? If it does, a package named
    `*_agent_pkg` that holds an env gets agent rules. If it doesn't, the presets' naming
    conventions only feed the consistency rules (§4.4).
  - Two strong content classifiers that disagree on one axis: a conflict finding (§3.2), or
    the sum (Q-CX3)? Under Q-CX3 they look like a mixed carrier, so the line between
    "conflict" and "mixed" needs drawing. A possible line: within one class it is a
    conflict, across classes it is a sum.
  - Is evidence **negative** too ("has no driver or monitor" counts against `agent`)?

  *To decide on the corpora* (P1.6/P1.7): measure how often content and convention disagree on
  `uvm-tb-proto`, `fw-wb-dma` and one OpenTitan IP.
- **Q-CX5. Units across carriers: identity and grouping.** An agent is a package plus an
  interface plus BFMs. The sub-questions:
  - **What joins a carrier to a unit:** content only (the package's driver, monitor or config
    gets `virtual X_if`), or also the name stem (`apb_` in `apb_if`, `apb_agent_pkg`)?
  - **Shared interfaces.** An interface is fetched by two units: an agent's monitor, and a
    second, passive agent or an env-level checker. Does it belong to both, or only to the unit
    whose *driver* uses it?
  - **Names.** Where does a unit's name (`apb`) come from: the package stem, the interface, or
    a tag? Two agents can share a stem (an `apb` host and an `apb` device agent).
  - **Nesting.** Is a sub-env inside an env one unit or two? Easier UVM requires an env to work
    both as a top and as a sub-env, which argues for two.

  *Proposed starting point:* content joins carriers; the name stem is weak evidence; an
  interface may belong to several units; the unit's name comes from a tag, else from its
  package's stem.

**Deferred:**
- **Q-CX6. Shared with PSS?** Maybe. Work the details out for SV first (user, 2026-10-16).
  pssparser's engine would gain the same selection-by-carrier mechanism; PSS has fewer
  contexts (library, component, `exec` targets).

## 8. Sources

The research behind §1, with URLs and what could not be verified, is
[`sv-uvm-rules-research/d-contexts.md`](sv-uvm-rules-research/d-contexts.md) (2026-10-16).
Two gaps matter here:
- the Verification Academy Cookbook's Agent and Dual-Top pages are members-only, so their
  public summaries were used;
- no public source says whether Verissimo can parameterise one rule per component type, or
  whether the commercial lint tools classify code as testbench or VIP.
