---
name: svcheck-rules
description: Write SystemVerilog and UVM lint rules for svcheck from a plain-language description. A rule is declarative (an `sv-rules` YAML file whose conditions are CEL expressions over slang's syntax trees or svcheck's elaborated model) and carries its own tests (`examples:`, checked by `svcheck --check-rules`). Use when the user wants a new SV or UVM lint rule, a house coding or naming rule, a rule pack, rule options, or to fix or test an existing rule file. Also covers the UVM pack's helpers (uvm.yaml) and contexts (applies_to), for rules about agents, envs and tests.
---

# Writing svcheck rules

A rule is data: YAML with each condition a CEL expression. svcheck loads it
with `--rules FILE`, type-checks it against the node kinds it matches, and
runs it on the design. Each rule carries `examples:`, code that must report
where marked and code that must not; `svcheck --check-rules` runs them.

## The loop

Follow it for every rule. Do not hand over a rule whose examples have not
passed.

1. **Pin the rule down in words.** What code must be reported, and at what
   (a call, a class, a statement)? What similar code must **not** be (the
   near misses)? What should the message tell the user to do? If the
   request is vague ("catch bad objections"), ask, or state the reading you
   chose.
2. **Pick the layer** (below): the model for what code *means*, syntax for
   how it is *written*.
3. **Find the kinds, fields and functions.** Search the references; don't
   guess from memory. A wrong guess costs one `--check-rules` run, which
   names the field and where it does exist.
4. **Write the examples first**, then the rule. One example that fires (each
   expected finding marked `// expect: ID`), one near miss with none.
5. **Check:** `svcheck --check-rules --rules acme.yaml --uvm UVM/src`. Exit
   0 means every expression type-checks and every example gives exactly its
   expected findings. Read each diagnostic: it gives a line and column *in
   the YAML*. Fix and repeat.
6. **Run it on real code:** `svcheck --rules acme.yaml -f tb.f --format json`
   (slang's own source arguments: `-f`/`-F`, `+incdir+`, `+define+`,
   `--top`). Read every finding. A wrong one is a near miss you missed: add
   it as an example, narrow the rule, back to 5. (Compiling a bench that
   will not compile, and selection, waivers and config: the `svcheck`
   skill.)

Where UVM's sources are: `--uvm` takes UVM's `src` directory (the one with
`uvm_pkg.sv`). An example that mentions `uvm_` is compiled with that
`uvm_pkg`. **Without `--uvm` it is compiled without UVM**: nothing derives
from `uvm_object`, so a UVM rule finds nothing and its examples fail with
"expected ... but it was not reported". Find UVM in the project (an
`ivpm`/`packages` directory, `$UVM_HOME/src`) or ask.

## A rule file

```yaml
# acme.yaml
sv-rules:
  version: 1
  uses: [uvm.yaml]          # the UVM pack's helpers: cls(), is_component(), ...
  rules:
    - id: ACME001
      name: display-in-component
      tier: style
      summary: A UVM component prints with a system task, not the report macros
      message: >-
        ${node.name} in ${cls(node).name}: use `uvm_info, `uvm_warning or
        `uvm_error, which the test's verbosity and report catcher control
      options:
        tasks:
          type: string-list
          default: [$display, $write]
          help: The system tasks to report
      match: { kind: Call }
      where: >-
        !in_library(node) && node.isSystem && node.name in options.tasks &&
        cls(node) != null && is_component(cls(node))
      examples:
        - code: |
            import uvm_pkg::*;
            `include "uvm_macros.svh"
            class my_driver extends uvm_driver;
              `uvm_component_utils(my_driver)
              function new(string name, uvm_component parent); super.new(name, parent); endfunction
              task run_phase(uvm_phase phase);
                $display("driving");             // expect: ACME001
                `uvm_info("DRV", "driving", UVM_LOW)
              endtask
            endclass
        - name: a sequence item may print
          code: |
            import uvm_pkg::*;
            class my_item extends uvm_sequence_item;
              function void show(); $display("item"); endfunction
            endclass
```

The keys, the tiers, options, `let`/`define`/`report`, `match.inside`, the
`expect` syntax and every load diagnostic: **`references/rule-file.md`**.
Read it before your first rule.

## Model or syntax

**The model** (`references/model.md`): the elaborated design, lowered to a
few kinds: `Package`, `Definition` (module, interface, program), `Class`,
`Subroutine`, `Variable`, `Process`, `Call`, `NewClass`, `Assign`, `Timed`
(anything that waits), `Block`, `If`, `Case`, `Loop` and so on. Names are
resolved (`Call.subroutine`, `NamedValue.decl`), types are known
(`type_of(e)`, `derives_from(c, "uvm_pkg::uvm_object")`), constants folded
(`const_int(e)`). Use it for meaning: "a class that derives from
uvm_component", "a call to `uvm_config_db::set`", "a delay inside
`run_phase`". A model rule needs the design to compile; where it does not,
the run says so once (`SVCHK231`).

**The syntax trees** (`references/syntax-kinds.md`): every file as
parsed, macros expanded, in slang's own kinds (`CaseStatement`,
`AlwaysBlock`, `FunctionDeclaration`), or by type (`BinaryExpressionSyntax`
is every binary operator). Use it for how code is written: a keyword, a
missing `default`, a qualifier, a form the model folds away. A syntax rule
runs even on a design that does not elaborate. `grep` the file for the
construct (`grep -n -i 'case' references/syntax-kinds.md`); it is too big to
read whole.

```yaml
sv-rules:
  version: 1
  rules:
    - id: ACME002
      name: plain-always
      tier: style
      summary: A plain always block, where always_comb, always_ff or always_latch says the intent
      message: use always_comb, always_ff or always_latch, which say what the block is for
      match: { kind: AlwaysBlock }
      where: macro_of(node) == ""
      examples:
        - code: |
            module m(input logic clk, input logic d, output logic q1, q2);
              always @(posedge clk) q1 <= d;       // expect: ACME002
              always_ff @(posedge clk) q2 <= d;
            endmodule
```

## From words to expressions

| The request says | Write |
|---|---|
| a UVM component / object / sequence / item / driver ... | `uses: [uvm.yaml]`, then `is_component(c)`, `is_object(c)`, `is_sequence(c)`, `is_item(c)`, `is_driver(c)` (`references/uvm-helpers.md`) |
| derives from a class | `derives_from(c, "pkg::name")`; of an expression's type: `derives_from(type_of(e), ...)` |
| inside a class / method / module | `cls(node)` / `method(node)` (uvm.yaml), or `enclosing(node, Class)`, `enclosing(node, Definition)`; `match: { kind: Call, inside: { kind: Class } }` |
| in `run_phase` | `method(node) != null && method(node).name == "run_phase"`; any run-time phase: `method(node).name in RUN_PHASES` (uvm.yaml) |
| a call to a method `f` | `match: { kind: Call }`, `node.name == "f"`. What it resolves to: `node.subroutine` (null for a system or built-in call); a UVM method: `node.subroutine.inLibrary`; `C::f()`: `node.classType.classDecl.qname == "pkg::C"` |
| a system task | `node.isSystem && node.name == "$display"` |
| `uvm_config_db#(T)::set/get` | `config_db(node) && node.name == "set"` |
| `T::type_id::create(...)` | `factory_create(node)` |
| the return value is ignored | `unchecked(node)` (uvm.yaml): a plain call statement, not `void'(...)` |
| an argument was (not) given | `!node.args[1].isDefault` |
| a delay or event wait | `match: { kind: Timed }`; `node.kind == TimingKind.Delay` (or `Event`, `Wait`, `WaitFork`: enum TimingKind in `references/model.md`) |
| a name pattern | `node.name.matches("^[a-z][a-z0-9_]*$")` (RE2, unanchored unless you anchor it), `endsWith`, `startsWith` |
| written by hand, not by a macro | `macro_of(node) == ""` |
| user code only | `!in_library(node)` (a call, statement) or `!node.inLibrary` (a declaration) |
| only in agents / envs / tests | `applies_to: { unit: agent }` with `uses: [uvm.yaml]` |
| statements after / before this one | `after(node)`, `before(node)` |
| configurable | `options:` and `options.<name>` |

Every function, with its signatures and what it returns for null:
`references/functions.md`.

## Pitfalls

Each of these has cost a rule author a round trip.

- **A leading `!` is a YAML tag.** `where: !node.isSystem` is `SVCHK218`.
  Write every expression as a block scalar (`where: >-`) or quoted.
- **Narrow before reading.** `node.receiver.name` is refused: `receiver`
  is an `Expr`, and only `Call` and `NamedValue` have `name`. Test the
  class first: `node.receiver.isa(NamedValue) && node.receiver.name == "x"`.
  `isa` narrows a variable (`node`, a `let`, a helper's parameter), not a
  function's result: put `parent(node)` in a `let` first.
- **Null.** A field of node type may be null (`Class.base`, a token the
  source does not have, `Call.subroutine` for a system call). Reading
  through null fails the rule at run time (`SVCHK220`). Guard with
  `x != null &&`, or read with `.?`.
- **Library code.** `uvm_pkg` and `--library-dir` code is declarations
  only: no bodies. Findings in it are not shown on a real run, but an
  example *does* see them: a rule on `Class` without `!node.inLibrary`
  reports every UVM class in the example and fails it. Always exclude it.
- **Compare classes, not type names.** A specialisation prints as
  `uvm_config_db#(int)`, without its package: compare
  `t.classDecl.qname == "uvm_pkg::uvm_config_db"`.
- **Examples compile alone**, with the rule's default options, and only
  that rule runs. A UVM example needs `import uvm_pkg::*;` (and
  `` `include "uvm_macros.svh" `` for the macros). Expect lines are
  counted in the example's own text.
- **IDs and names are shared** with svcheck's built-in rules: `SVCHK` is
  reserved, and a name the UVM pack uses (`objection-in-agent`) is
  `SVCHK217`. `svcheck --list-markers` lists every name taken.
- **Tier sets severity**: `style` is a hint (never fails a run),
  `correctness`/`suspicious` a warning. Choose by how sure a finding is.

## Running the rules

A `--rules` file's rules run in every profile but `none`, without
`--select`. `--ignore ACME002` turns one off. A run with a warning exits
1, one with only hints 0; `-Werror` makes warnings errors (hints stay
hints). A project names its files in a configuration:

```yaml no-check
# svcheck.yaml, read with --config svcheck.yaml
svcheck:
  rules: [lint/acme.yaml]
  rule:
    display-in-component: { tasks: [$display, $write, $fdisplay] }
```

Useful while writing:

- `svcheck --rules acme.yaml --explain ACME001`: the rule as loaded, with
  its options and where it is defined.
- `svcheck --rules acme.yaml --rules-for tb/x.sv`: which rules a file gets.
- `svcheck --list-markers`: every rule svcheck knows, built-in ones too.
- `svcheck --explain SVCHK-UVM037`: a built-in UVM rule; the pack's 39
  rules are good models (`--select SVCHK-UVM` runs them).
- `svcheck --rules acme.yaml -f tb.f --contexts`: the units the
  classifiers found (agent, env, test, ...), for `applies_to`.

## Checklist

- [ ] An ID prefix of your own (2-5 capitals + 3 digits), a kebab-case name.
- [ ] `summary` one line; `message` says what is wrong *and* what to do.
- [ ] `!in_library(node)` / `!node.inLibrary`, unless library code is the point.
- [ ] At least one example that fires and one near miss; the near miss is
      the case most likely to be reported wrongly.
- [ ] `svcheck --check-rules --rules FILE --uvm UVM/src` exits 0.
- [ ] Run on real code; every finding read; false positives turned into
      examples.
