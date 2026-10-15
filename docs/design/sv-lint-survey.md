# Declarative lint for SystemVerilog: survey and assessment

Status: research report, 2026-10-10. Nothing here is decided.
Companion to `portable-lint-rules.md`, `pss-rules-format.md` and `lint-rule-expressions.md`
(the accepted PSS direction: a core lint engine, rules as YAML plus a CEL subset, and a closed
library of semantic functions).

The question: is the `pss-rules` idea worth extending to SystemVerilog, and would it offer
anything the plugin-extensible SV linters do not? The motivating case is
`featherweight-hdl/fw-hdl`: an SV class library with rules about content (a static,
synthesisable subset) and relationships (components, ports, exports, channels, runnables). The
aim is to add those rules to a checker without significant code work.

## 0. Answer

> **Short version.** Yes, it is worth doing, as a *second host* for the same engine rather than
> as a new product. No SV linter we found, open or commercial, lets a user write **semantic**
> rules as data and run the same rules in the editor and in batch (§2). The SV linters offer
> regex-configured naming rules plus code: C++ rebuilds (Verible, slang-tidy), Rust dylibs
> (svlint), Python (pyslang), or vendor APIs in Perl/C (SpyGlass), Java (Verissimo) and C++
> (ALINT-PRO). Slang is a good substrate. Its syntax tree is generated
> from a data file, `scripts/syntax.txt`, as ours is from `ast/*.yaml`, and its semantic layer
> resolves names, types and constants.
>
> **What it would offer that plugins do not:**
> 1. **Library-carried rules.** fw-hdl ships `rules/fw-hdl.yaml` next to its source, and
>    anyone who compiles against fw-hdl gets those rules.
> 2. **Safety.** The rules are bounded data, not code, so an editor can load a dependency's
>    rules without a trust prompt.
> 3. **One answer everywhere.** The same rules give the same findings in the CLI, CI (SARIF)
>    and the editor.
>
> **What to do now:** design F3's kernel so it holds no PSS types (§5.1). That costs little
> while F3 is unwritten and keeps the SV option open. Don't build the SV host yet. When wanted,
> run a two-week spike: a slang host with 10 rules, 5 from Verible and 5 from fw-hdl (§5.3).

## 1. The fw-hdl case

### 1.1 What fw-hdl enforces today, and how

fw-hdl's front end (`python/fw/hdl/fe/`) is built on pyslang. Its restrictions are enforced as
**fail-fast compiler errors**: about 150 `raise reporter.fail(...)` sites, most of them in
`fe/static_mapper.py`. Consequences:

- A user learns about one violation per run, at synthesis time, from a batch flow.
- The rules are not visible in the editor.
- There is no rule ID, waiver, `--explain` or catalogue.
- The rules are written as mapper code, so they are not documentation either.

Some restrictions have no check at all. `fw_component.svh:306` documents one: "a model where
nothing happens because someone forgot add_runnable() is indistinguishable from one that is
merely idle". The library detects this only at run time, from the *absence* of a debug event.

The rules fall into two groups (from `docs/methodology/components.md`, `synthesis.md` and
`static_mapper.py`):

| Kind | Rule | What it needs |
|---|---|---|
| content | 2-state only: `logic` wider than 1 bit is rejected | `type_of`, `isFourState`, `bitWidth` |
| content | `casez`/`casex` are rejected | syntax: the case kind |
| content | no `#`, `@`, `wait` in the static subset; no `break`/`continue`; locals are automatic | syntax plus enclosing method |
| content | `/` and `%` only by a non-zero constant or under a `d != 0` guard | `const_value`, plus a guard pattern (§1.3) |
| content | packed ranges descending; unpacked arrays `[N]` or `[0:N-1]` | `const_value` on range bounds |
| content | no non-blocking assignment in a class method | syntax plus enclosing class |
| relationship | channel operations (`get`/`put`/`try_get`) only in `run()` | resolve call → port type; enclosing method |
| relationship | `try_get` only on a port declared `fw_port #(fw_get_nb_if #(T))` | type parameter of the port's specialisation |
| relationship | a component that defines `run()` calls `add_runnable(this)` in its constructor, and a structural component does not | method presence ⇔ call in `new` |
| relationship | `.connect()` only inside `connect()`; an export never connects to a port | resolve receiver/argument types; enclosing method |
| relationship | no `get`/`put` inside a loop within the activation; at most one op per channel per activation | ancestors, `dups` over resolved ports |
| relationship | every child of a structural component is assigned to an engine (SPL flow) | elaboration-level; probably stays in the flow |

Most of these need **semantic facts**: which class a component derives from, the type
parameter of a port specialisation, and what a call resolves to. A syntax-only linter (Verible,
svlint) cannot see them. A regex rule is not even close.

### 1.2 What "augment an existing checker" costs today

| Route | What adding fw-hdl's rules takes | Semantic? | In the editor? |
|---|---|---|---|
| Verible | Write C++ rules against its CST matcher combinators, then rebuild Verible with Bazel or maintain a fork; the maintainers confirm "there is no plug-in system" (issue #1999). Configuration only reaches parameters such as regexes. | no (CST) | yes, via the Verible LSP, but only for rules compiled into that build |
| svlint | Rust rules, as a fork or a `--plugin` cdylib built against the same svlint version and compiler (§2) | no (CST) | yes: svls loads the same plugins |
| slang-tidy | C++ `TidyCheck` class + registration + diag + CMake entry, then rebuild slang | yes (elaborated) | no (slang-server shows compiler diagnostics, not tidy checks) |
| Verilator `--lint-only` | fixed warning set; no user rules | partly | no |
| pyslang script | what fw-hdl already does: a visitor per rule, run in batch | yes | no |
| Commercial | SpyGlass: Perl + C primitives; Verissimo: Java API (docs via support); ALINT-PRO: C++ API (older docs); most others configure built-in rules only (§2.2) | yes, where an API exists | Verissimo runs its rule sets incrementally in DVT; Java rules not confirmed |

Every route except pyslang means **compiled code in someone else's tool**, and the pyslang
route gives no editor, IDs, waivers or catalogue. This is the gap the user described.

### 1.3 fw-hdl's rules as data

The rules below are written in the PSS format's style against slang's **semantic** AST. Node
kinds and fields follow slang's C++ names (`CallExpression`, `ClassType`, `Subroutine`,
`CaseStatement.condition`); in a real host they would come from a generated schema. The library
functions with the same names as PSS functions (`type_of`, `const_value`, `resolves_to`,
`enclosing`, `ancestors`, `dups`) mean the same things. The SV-only ones are `derives_from`,
`method`, `calls` and `type_arg`. The single root key is `sv-rules:` (see §5.2), laid out like
`pss-rules:` (format §2).

```yaml
sv-rules:
  version: 1

  defaults:
    tier: correctness
    since: "2026.1"

  define:
    component(n): enclosing(n, ClassType)
    is_component(c): has(c) && derives_from(c, "fw_hdl_pkg::fw_component")
    # `in.t.get(x)`: a call whose receiver is member `t` of an fw_port specialisation.
    port_of(call): >-
      call.thisClass.kind == "MemberAccess" && call.thisClass.member.name == "t" &&
      derives_from(type_of(call.thisClass.value), "fw_hdl_pkg::fw_port")
      ? call.thisClass.value : null
    port_api(call): type_arg(type_of(port_of(call)), 0)
    in_method(n, name): enclosing(n, Subroutine).name == name

  rules:
    - id: FWH001
      name: forgot-add-runnable
      family: structure
      summary: A component defines run() but never registers itself as runnable
      message: >-
        '${node.name}' defines run() but its constructor does not call add_runnable(this);
        run() will never be forked
      doc: |
        A runnable is not detected automatically (fw_component.svh). Without
        add_runnable(this) in the constructor the component builds and connects, then does
        nothing, which looks exactly like an idle model.
      match: { kind: ClassType }
      where: >-
        is_component(node) && has(method(node, "run")) &&
        !calls(method(node, "new")).exists(c, c.subroutine.name == "add_runnable" &&
                                              c.arguments[0].kind == "This")

    - id: FWH002
      name: channel-op-outside-run
      family: structure
      summary: A port operation outside run()
      message: >-
        '${node.subroutine.name}' on port '${port_of(node).symbol.name}' in
        ${enclosing(node, Subroutine).name}(); channel operations are only allowed in run()
      match: { kind: CallExpression }
      where: >-
        is_component(component(node)) && port_of(node) != null &&
        node.subroutine.name in ["get", "put", "try_get"] && !in_method(node, "run")

    - id: FWH003
      name: try-get-needs-nb-port
      family: structure
      summary: try_get() on a port that is not an fw_get_nb_if port
      message: >-
        try_get() needs the port declared fw_port #(fw_get_nb_if #(T)); '${port_of(node).symbol.name}'
        is fw_port #(${port_api(node).name})
      match: { kind: CallExpression }
      where: >-
        node.subroutine.name == "try_get" && port_of(node) != null &&
        !derives_from(port_api(node), "fw_std_pkg::fw_get_nb_if")

    - id: FWH004
      name: export-connects-to-port
      family: structure
      summary: An export connected to a port
      message: "an export connects to an export below it, never to a port"
      match: { kind: CallExpression }
      where: >-
        node.subroutine.name == "connect" && has(node.thisClass) &&
        derives_from(type_of(node.thisClass), "fw_hdl_pkg::fw_export") &&
        derives_from(type_of(node.arguments[0]), "fw_hdl_pkg::fw_port")

    - id: FWH005
      name: four-state-in-static-subset
      family: subset
      tier: subset
      summary: A 4-state vector in a component
      message: "'${node.name}' is ${type_of(node).name}; the static subset is 2-state, use bit"
      match: { kind: [ClassProperty, Variable] }
      where: >-
        is_component(component(node)) &&
        type_of(node).isFourState && type_of(node).bitWidth > 1

    - id: FWH006
      name: wildcard-case
      family: subset
      tier: subset
      summary: casez/casex in a component
      message: "${node.condition == 'WildcardJustZ' ? 'casez' : 'casex'} is not in the static subset; use case"
      match: { kind: CaseStatement }
      where: is_component(component(node)) && node.condition in ["WildcardJustZ", "WildcardXOrZ"]

    - id: FWH007
      name: unguarded-division
      family: subset
      tier: subset
      summary: Division by a value that is neither constant nor guarded
      message: "divisor is not a constant and not under a 'd != 0' guard"
      match: { kind: BinaryExpression }
      define:
        guards(a, d): >-
          a.kind == "ConditionalStatement" && a.conditions.exists(c,
            c.expr.kind == "BinaryExpression" && c.expr.op == "Inequality" &&
            resolves_to(c.expr.left) == resolves_to(d) &&
            const_value(c.expr.right).orValue(1) == 0)
      where: >-
        node.op in ["Divide", "Mod"] && is_component(component(node)) &&
        !const_value(node.right).hasValue() &&
        !ancestors(node).exists(a, guards(a, node.right))
```

What this shows:

- **Relationship rules are cheap once the functions exist.** FWH001 to FWH004 are 3–6 lines of
  predicate each. In Verible each would be a C++ class, and without semantics a wrong one.
- **The bounds show.** FWH007 accepts a divisor guarded in the `else` branch (it does not check
  which branch the node is in), and it does not see a guard established by an earlier
  `if (d == 0) return;`. That is dataflow. The escape hatch is a library function,
  `nonzero_at(expr)`, written once in C++ in the host, the same route PSS rules take
  (lint-rule-expressions §3).
- **Some rules stay in the flow.** "Every child is assigned to an engine" depends on the
  partitioner's configuration, not on the source. Lint is the wrong place for it.
- **Specialisation needs care.** `port_api` reads the type argument of a class specialisation.
  Inside a parameterised component (`class c #(type T)`) that argument may be `T` itself, and
  the rule must decide whether to check the generic body, each specialisation, or both
  (§4, S-2).

## 2. Survey: SystemVerilog linters and custom rules

Two surveys were run on 2026-10-10 from primary sources: repositories, source, issues, vendor
documentation and DVCon papers. Much commercial documentation sits behind support logins, so
"not found" below means not found publicly.

### 2.1 Open source

| Tool | How a user adds a rule | Data-defined rules | Semantics | Same rules in editor | Notes |
|---|---|---|---|---|---|
| **Verible** (CHIPS Alliance, Apache-2.0) | C++ class + compiled matcher combinators; rebuild. "For now, there is no plug-in system available" (maintainer, [#1999](https://github.com/chipsalliance/verible/issues/1999)) | parameters only (`--rules="enum-name-style=style_regex:..."`, `.rules.verible_lint`); forbidden lists hard-coded | CST, per file | yes (`verible-verilog-ls`) | OpenTitan's style linter; waivers inline and by file |
| **svlint / svls** (MIT) | Rust `SyntaxRule`; `--plugin` cdylib exporting `get_plugin() -> Vec<Rule>` (version-locked in practice) | 56 `re_forbidden_*`/`re_required_*` regexes, each tied to one declaration kind | syntax (preprocessed) | yes (svls loads plugins) | quiet since 2025-11 |
| **slang-tidy** (MIT) | C++ `TidyCheck::check(RootSymbol, AnalysisManager)` + registration + diag + CMake; rebuild | `CheckConfigs` names and regexes | **elaborated, typed** | no (slang-server shows compiler diagnostics only) | slang v12.0, 2026-09-29; `--lint-only` mode; official WASI build |
| **pyslang** | Python visitors | none | elaborated, typed | no | [pyslint](https://github.com/AsFigo/pyslint) (~30 rules, "do NOT use this in production") |
| **Verilator** | none (`--json-only` AST dump to build your own) | suppression only (`.vlt`) | elaborated | via others' LSPs | |
| **Surelog/UHDM** | C++ `VpiListener` (`UhdmLint`) | none | elaborated (VPI) | no | |
| **ast-grep + tree-sitter SV** | YAML rule (`kind`, `inside`, `has`, `not`, `stopBy`) via a custom-language `.so` | **yes** | **syntax only**: no preprocessor, names or types | ast-grep LSP (custom languages unconfirmed) | hobby-scale: [ast-grep-svlint](https://github.com/coderkalyan/ast-grep-svlint), [svuff](https://github.com/zoomer-k/svuff) |
| **VSG** (VHDL, GPL-3) | Python `local_rules` | YAML/JSON parameters | tokens | — | the same configure-in-data, extend-in-code split |
| Semgrep, CodeQL | — | — | no HDL support | — | |

### 2.2 Commercial

| Tool | How a user adds a rule | Declarative part | Same rules live in editor |
|---|---|---|---|
| **SpyGlass** (Synopsys) | Perl policy files that select and parameterise **C primitives**; real checks are written in C. "RTL Rule Builder", $50k in 2000 | GuideWare goals (`define_goal ... set_parameter`) | no (Euclide is a separate engine) |
| VC SpyGlass | Tcl shell for querying; user rule API not confirmed publicly | goals and parameters | no |
| Cadence HAL / Jasper Superlint | HAL: VPI/VHPI/CPI (C); HAL is "legacy, replaced by Superlint". Superlint: no public rule API found | `design_info` patterns; Tcl `config_rtlds` | no |
| Questa Lint (Siemens) | none found ("configure methodologies") | prebuilt check sets | no |
| **HDL Designer DesignChecker** (Siemens) | none: **parameterised base rules** (Naming, Allowed Operators, Case Style, ...) composed into rule sets and policies | the most declarative commercial tool, but only over vendor building blocks | within HDL Designer |
| Ascent Lint (Real Intent) | none found | hierarchical policies, inherited down the design | no |
| Blue Pearl | Tcl procedure writes messages to its database | Tcl packages; XML waivers | VS Code extension shells out to the CLI and shows SARIF |
| **Verissimo / DVT** (AMIQ) | **Java API** over DVT's semantic database; the public doc is one sentence, the rest via support | XML rule sets (`<include>`, severity, skips; UVM component kinds by base class and name pattern); XML and inline waivers; baselines | **yes**: incremental linting after each build with the same rule-set XML. Not confirmed for Java custom rules |
| Sigasi Visual HDL | none | JSONC settings; naming conventions as valid/invalid regex pairs per identifier kind | yes: IDE and CLI share settings |
| ALINT-PRO (Aldec) | C++ API in older datasheets; the current one omits it | Policy Editor, typed parameters, Tcl | own GUI |
| Defacto STAR | Python/Tcl/C++ over connectivity, for in-house DRC engines | — | no |
| Verific (LintRTL, and as the base of in-house linters) | C++ parser/elaborator API | configurable rule packs | — |

### 2.3 What the surveys show

1. **Nobody offers semantic rules as data.** Every tool that reaches semantics (slang-tidy,
   pyslang, UHDM, SpyGlass C, Verissimo Java, ALINT C++, HAL VPI) needs code. Every tool that
   accepts data rules is syntax-only (ast-grep) or tunes fixed rules (regexes, parameters, base
   rules). We found no CEL, XPath or Datalog layer over an SV AST, open or commercial.
2. **The nearest structural precedent is SpyGlass.** A closed library of vendor-written C
   primitives, composed and parameterised by user-written policy files. That is our
   "library functions plus data rules" split, except that the composition language is Perl
   (unbounded code) and the primitives were sold with a $50k rule kit.
3. **Same rules in editor and batch exists only for built-in, configured rules.** Verissimo,
   Sigasi, Verible LS and svls all run their rules in the editor. None documents a
   user-authored *semantic* rule running unchanged in both. Blue Pearl's VS Code integration
   shells out to the batch tool.
4. **Custom rules are wanted but costly.** The PySlint DVCon paper says "many customers tend
   to develop their own rules/policies primarily based on UVM", and that "one of the major
   bottlenecks in implementing custom rules … has been the lack of easy-to-access
   SystemVerilog parsers". Vendor rule APIs sit behind support logins: AMIQ's is one public
   sentence, and ALINT-PRO's has dropped off its datasheet. Custom rule writing today is a
   CAD-team activity, not something a library author does.
5. **Noise and waivers dominate the complaints** (Cadence DVCon 2021; Real Intent 2026 case
   study, "hundreds of false violations" on third-party IP; Phison DVCon Taiwan 2025, 9,448
   hits on one STARC rule). That argues for keeping the nursery/promotion discipline, waivers
   and baselines in the kernel (plan F2/F3.10) when moving to SV.

## 3. Could Verible's rules be written declaratively against slang?

Verible ships 61 lint rules (`verible/verilog/analysis/checkers/`, 7,649 lines of C++ excluding
tests, as of the local checkout, January 2025). Every rule is a C++ class registered with
`VERILOG_REGISTER_LINT_RULE`. Configuration reaches only declared parameters: 17 rules take
parameters, mostly regexes. Even the "forbidden" rules hard-code their lists. For example,
`forbidden-symbol-rule.cc` keeps `$psprintf`, `$random` and the `$dist_*` functions in a
`std::map` in the source.

The rules sorted by what a declarative version against slang would need:

| Group | Count | Rules | Declaratively? |
|---|---|---|---|
| A. Naming and style regex | 13 | `banned-declared-name-patterns`, `constraint-name-style`, `enum-name-style`, `interface-name-style`, `macro-name-style`, `parameter-name-style`, `parameter-type-name-style`, `signal-name-style`, `struct-union-name-style`, `port-name-suffix`, `positive-meaning-parameter-name`, `generate-label-prefix`, `create-object-name-match` | **Yes, trivially**: kind + `matches()` + an `options:` regex. `port-name-suffix` uses the port direction, which slang has. `create-object-name-match` compares a call's string argument with the assigned variable's name, which is one line of CEL. |
| B. Structural (kind, field, inside) | 28 | `always-comb`, `always-comb-blocking`, `always-ff-non-blocking`, `case-missing-default`, `explicit-begin`, `explicit-function-lifetime`, `explicit-task-lifetime`, `explicit-function-task-parameter-type`, `explicit-parameter-storage-type`, `forbid-consecutive-null-statements`, `forbid-defparam`, `typedef-enums`, `typedef-structs-unions`, `forbid-negative-array-dim`, `generate-label`, `legacy-generate-region`, `legacy-genvar-declaration`, `module-begin-block`, `module-parameter`, `module-port`, `mismatched-labels`, `proper-parameter-declaration`, `suspicious-semicolon`, `v2001-generate-begin`, `void-cast`, `invalid-system-task-function`, `forbidden-macro`, `disable-statement` | **Yes.** These are the `match:` + `where:` shape exactly. Slang's syntax tree has every field needed (`FunctionPrototype.lifetime`, `CaseStatement.items` with `DefaultCaseItem`, `DefParam`, `GenerateRegion`, `GenerateBlock.label`, `EmptyStatement`, …). Two get *better* with semantics: `always-ff-non-blocking`'s `waive_for_locals` becomes "the target resolves to a block-local variable" rather than a syntactic guess, and `disable-statement` resolves the label to the block it names. The forbidden lists become `options:`. |
| C. Ranges and literals | 5 | `packed-dimensions-range-ordering`, `unpacked-dimensions-range-ordering`, `truncated-numeric-literal`, `undersized-binary-literal`, `numeric-format-string-style` | **Yes, with library help.** Range ordering needs `const_value` on the bounds, which slang provides (Verible guesses from the text). The literal rules need a `literal_info(expr)` function (declared width, base, digit count) and the format-string rule a `format_specs(str)` function. These are the PSS pattern: a small C++ function, then the rule is data. |
| D. File-level | 3 | `module-filename`, `package-filename`, `one-module-per-file` | **Yes** with `file_of(node)` and a per-file match (`kind: CompilationUnit`). |
| E. Preprocessor | 4 | `endif-comment`, `macro-string-concatenation`, `uvm-macro-semicolon`, `forbid-line-continuations` | **Partly.** Slang keeps directives as trivia nodes (`MacroUsage`, `DefineDirective`, `ConditionalBranchDirective` are in `syntax.txt`), so `endif-comment` is a match on directive trivia. `uvm-macro-semicolon` and `macro-string-concatenation` look at the tokens *around and inside* a macro use, which a node-matching engine sees poorly. They need a token-level view, or they stay native. |
| F. Text | 4 | `line-length`, `no-tabs`, `no-trailing-spaces`, `posix-eof` | **Not the engine's job.** These are formatter rules, and pssfmt already owns them for PSS. A host can offer `kind: SourceLine` if wanted, but it is a different kind of rule. |
| G. Scope | 1 | `instance-shadowing` | **Yes**, and simpler with semantics: `dups()` over the names declared in a scope. |
| H. Multi-construct heuristics | 3 | `dff-name-style`, `suggest-parentheses`, `explicit-begin`'s 11 toggles (counted in B) | **Yes, but long.** `dff-name-style` pairs the LHS and RHS suffixes of each `<=` in an `always_ff` and has four options. It is about 20 lines of YAML: expressible, but it is the size where a native rule starts to compete. |

The groups overlap slightly (`explicit-begin`), so the counts are approximate. The tally:

- About **50 of 61** go straight into data.
- About **6** need one small library function each (`literal_info`, `format_specs`, `file_of`,
  directive trivia access).
- About **4** (the text rules) do not belong in a lint engine.
- About **2** (`uvm-macro-semicolon`, `macro-string-concatenation`) need token-level access
  and would stay native or be dropped.

Most of what Verible needs C++ for is *traversal plumbing*: matcher setup, context stacks and
violation bookkeeping. A 120-line C++ rule becomes a 5–15 line YAML rule. Verible's checkers
average about 125 lines each, excluding tests.

Two caveats:

- **Verible lints code it cannot elaborate.** It works per file, with no include paths, macros
  or library. That is why it is so easy to deploy (OpenTitan runs it on every file). A slang
  host needs a compilable unit or must run in a degraded, syntax-only mode. Slang can parse a
  single file and report a syntax tree without elaborating, so the syntax-only groups (A, B, D,
  E) would still run. The semantic rules (fw-hdl's, `disable-statement`, the range rules)
  would need the file list. This is the same two-level split as PSS: L1/L2 (single file) and
  L3/L4 (linked).
- **Slang already reports many of the semantic hazards** that SV lint tools exist for (unused,
  width truncation, implicit nets, missing case items, and others; see its warning list,
  `scripts/warning_docs.txt`). A slang host would not reimplement those. It would map slang's
  own warnings into the same selection, waiver and SARIF machinery, as `PSS` and the
  core-emitted `LINT` rules are mapped in pssparser.

## 4. What an SV host involves

The PSS engine (plan F3) splits naturally into two parts. The **kernel** holds the YAML loader,
the CEL parser, checker and evaluator, dispatch, budgets, selection, waivers, examples-as-tests
and SARIF. The **host binding** holds the node schema, field accessors and the function
library. An SV host is a second binding:

| Piece | PSS host | SV (slang) host |
|---|---|---|
| Syntax schema | generated from `ast/*.yaml` | generated from slang's `scripts/syntax.txt` (2,386 lines, data-driven, `kindmap<>` groups map onto `kind: [...]` lists) |
| Semantic nodes | pssparser's linked AST | slang's `Symbol`, `Expression`, `Statement`, `Type`, `TimingControl` (hand-written C++; the kinds are enumerated, and `ASTSerializer` already visits every property, so an accessor table can be generated or written once) |
| Resolution | S1 `resolves_to`, `decl_path` | `Expression` → `Symbol` is direct in slang; `Symbol::getHierarchicalPath` |
| Types and constants | `type_of`, `const_value` | `Type`, `ConstantValue` (slang's constant evaluator) |
| Inheritance | `bases`, `extensions` | `ClassType::getBaseClass`, `implements` lists; `derives_from` |
| Text | — | directive trivia, `file_of`, optional `SourceLine` |
| WASM | `@psstools/pssparser` | slang's CMake has official WASI support (tests run under `wasmtime`), and its online explorer runs in the browser; an Emscripten/browser build for an LSP and its size are untested |

**Size guess:** the kernel is shared. The slang binding is a schema generator for `syntax.txt`,
semantic accessors, and about 20 library functions: roughly 3–5k lines of C++. That is
comparable to the PSS function library, and smaller than the kernel.

Problems a slang host has that the PSS host does not:

- **S-1, elaboration multiplicity.** A module body is elaborated once per unique parameter set,
  so a rule over the semantic AST can fire several times for one source line. Findings must be
  de-duplicated by syntax location, and a rule whose truth depends on the parameters must be
  able to say which instance it saw. Uninstantiated modules need slang's "elaborate as if
  uninstantiated" mode.
- **S-2, class specialisation.** Class bodies are generic until specialised. Rules can run on
  the generic body (types may be `T`) or per specialisation (multiplicity again). fw-hdl's
  port-type rules need the specialisation.
- **S-3, macros.** Findings inside a macro expansion must map to the use site or be suppressed.
  Slang's `SourceManager` tracks expansion locations, so this is policy, not capability.
- **S-4, compilation context.** Semantic rules need the file list, defines and include paths,
  which in an editor means a project file. pssparser's LSP has the same question at a smaller
  scale.
- **S-5, slang API churn.** fw-hdl's own projects pin pyslang from `>=3.0` to `>=7.0`. The
  syntax schema regenerates from `syntax.txt`, but the semantic accessors are hand-maintained
  against a moving API. Pin a slang version per host release.

## 5. Recommendation

### 5.1 Now: keep the kernel language-neutral (cost: low)

While F3.1–F3.3 are being written, make these design constraints:

- The kernel (loader, CEL, runner, selection, waivers, catalogue, SARIF, examples) has **no
  pssparser AST includes**. It talks to a `Host` interface: a schema table (kinds, fields,
  `kindmap`-style groups), a node handle (kind, field get, children, parent, location), and a
  function registry. PSS is the first implementation.
- The CEL type checker takes its schema from the host, not from generated PSS headers.
- The example runner calls the host to parse a snippet.
- Rule IDs, namespaces (including Q-F1a's `ACME-NET001`), tiers, severities and waivers stay
  kernel concepts.

This is mostly how F3 would be built anyway, since the schema is already generated from
`ast/*.yaml`. Written down, it costs little now and avoids an expensive untangling later. It
adds a few F3 checklist items, not a phase.

### 5.2 The file format

The root key `pss-rules:` should not become `sv-rules:` by copy-and-edit. Two choices:

- Keep a root key per host (`pss-rules:`, `sv-rules:`), with the same keys under it. A file self-identifies its language, and a
  PSS tool given an SV rule file says so.
- One kernel key with a language field.

**Recommendation:** a key per host. It keeps today's spec intact, and a rule set is
inherently tied to one schema. The kernel parses both with one grammar.

### 5.3 Later: an SV spike (about 2 weeks, when wanted)

1. A slang host with the syntax schema generated from `syntax.txt`, plus semantic accessors for
   classes, subroutines, calls, assignments and case statements.
2. Ten rules: 5 from Verible (`case-missing-default`, `always-ff-non-blocking`,
   `explicit-function-lifetime`, `signal-name-style`, `disable-statement`) and 5 from fw-hdl
   (FWH001–FWH004 and FWH006).
3. Run them over fw-hdl's tests and examples, and over OpenTitan's `hw/ip` for scale and
   multiplicity (S-1).
4. Measure the native-versus-WASM time and size.

**Go/no-go:**

- every rule fits in 15 lines;
- findings match Verible's on the shared rules, or differ explainably because of semantics;
- elaboration multiplicity is handled;
- the WASM build fits a size budget to be set then.

### 5.4 Where the rules and the host would live

- **The kernel:** inside pssparser (`src/lint/`) until a second host exists. When the SV host
  is real, move it to a small shared library that both hosts depend on.
- **The SV host:** its own repository (for example under `fvutils`), depending on slang and the
  kernel.
- **fw-hdl's rules:** `fw-hdl/rules/fw-hdl.yaml`, in the library, versioned with it. A project
  that uses fw-hdl gets them through its package manifest (`ivpm.yaml` or `flow.yaml`). This is
  the "library-carried rules" property.

### 5.5 Positioning

- **What it is:** methodology and library-usage rules, and house style, written by the people
  who own the library or the methodology, running everywhere.
- **What it is not:** sign-off RTL lint. CDC, synthesis-semantics and power rule decks stay
  with SpyGlass, Ascent and their peers. Don't compete there.

The closest precedent for library-carried rules is outside EDA. Roslyn analyzers ship inside
NuGet packages and Android Lint checks ship inside AARs. Both enforce correct use of a library,
in the IDE and the build, but both are compiled code. Making them bounded data is the new part.

## 6. Open questions

- **Q-SV1.** Build the SV host at all, and when? This report recommends only §5.1 now.
- **Q-SV2.** A key per host (`sv-rules:`) or one key with a language field (§5.2)?
- **Q-SV3.** Should the fw-hdl compiler run the same rule pack as its front-end check? That
  would replace the hand-coded `raise` sites for the rules data can express. It would remove
  duplication but put the compiler on the lint engine's release train.
- **Q-SV4.** Where does a degraded, syntax-only mode stop? Should a semantic rule be skipped
  silently, or reported as "not run", when there is no file list?

## 7. Sources

- Verible rules: local checkout `~/projects/verible/verible`, `verible/verilog/analysis/checkers/`
  (descriptors and parameters extracted 2026-10-10).
- slang syntax definitions: `MikePopoloski/slang` `scripts/syntax.txt`, fetched 2026-10-10;
  `scripts/warning_docs.txt`.
- fw-hdl: `~/projects/featherweight-hdl/fw-hdl`: `docs/methodology/components.md`,
  `docs/methodology/synthesis.md`, `python/fw/hdl/fe/static_mapper.py`, `src/fw_component.svh`.
- Survey sources: linked inline in §2.1–§2.2. Also: SpyGlass history (EE Times, 2000 and 2003); PySlint, DVCon ([92146.pdf](https://dvcon-proceedings.org/wp-content/uploads/92146.pdf)); Verissimo docs (eda.amiq.com/documentation/verissimo); Sigasi manual (sigasi.com/manual); Real Intent multi-policy case study (design-reuse.com, 2026).
