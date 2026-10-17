# The rule file (`sv-rules`)

A rule file is YAML with one top-level key, `sv-rules`. svcheck reads it
with `--rules FILE` (as often as needed) or from a configuration's
`rules:` list, and `svcheck --check-rules --rules FILE --uvm UVM/src`
checks it: every key, every expression's types, every example.

## A worked file

Two rules: one reads the model inside classes, one runs only in agent
units. Both use the UVM helpers (`uses: [uvm.yaml]`, `uvm-helpers.md`).

```yaml
sv-rules:
  version: 1
  uses: [uvm.yaml]
  defaults: { tier: suspicious, family: uvm }
  define:
    in_body(n): method(n) != null && method(n).name == "body"
  rules:
    - id: ACME010
      name: literal-delay-in-sequence
      summary: A sequence waits a fixed delay, which ties it to one clock period
      message: >-
        '${cls(node).name}.body' waits a literal delay; wait on the interface's
        clock or an event instead
      match: { kind: Timed, inside: { kind: Class } }
      let:
        k: cls(node)
      where: >-
        !in_library(node) && node.kind == TimingKind.Delay &&
        is_sequence(k) && in_body(node) && const_int(node.exprs[0]).hasValue()
      examples:
        - code: |
            import uvm_pkg::*;
            class my_seq extends uvm_sequence;
              int unsigned gap;
              task body();
                #10;                               // expect: ACME010
                #(gap);
              endtask
            endclass
        - name: a component may wait
          code: |
            import uvm_pkg::*;
            class my_drv extends uvm_driver;
              function new(string name, uvm_component parent); super.new(name, parent); endfunction
              task run_phase(uvm_phase phase); #10; endtask
            endclass

    - id: ACME011
      name: agent-sets-config
      summary: An agent's code sets a config_db entry
      message: >-
        an agent sets '${const_string(node.args[2]).orValue("?")}' in config_db; the env or the test owns
        the configuration
      applies_to: { unit: agent }
      match: { kind: Call }
      where: '!in_library(node) && config_db(node) && node.name == "set"'
      examples:
        - code: |
            import uvm_pkg::*;
            package bus_agent_pkg;
              import uvm_pkg::*;
              class bus_agent extends uvm_agent;
                function new(string name, uvm_component parent); super.new(name, parent); endfunction
                function void build_phase(uvm_phase phase);
                  uvm_config_db#(int)::set(this, "*", "depth", 4);   // expect: ACME011
                endfunction
              endclass
            endpackage
        - name: the env sets it
          code: |
            import uvm_pkg::*;
            package top_env_pkg;
              import uvm_pkg::*;
              class top_env extends uvm_env;
                function new(string name, uvm_component parent); super.new(name, parent); endfunction
                function void build_phase(uvm_phase phase);
                  uvm_config_db#(int)::set(this, "agent*", "depth", 4);
                endfunction
              endclass
            endpackage
```

- `defaults:` sets keys for every rule of the file (here `tier` and
  `family`); a rule's own key wins.
- The file's `define:` helpers (`in_body`) are functions every rule of the
  file can call, as are those of each file it `uses`.
- `let:` names sub-expressions, evaluated in order; each can be used in
  `where`, `report` and `message`, and **narrows** like a variable.
- `const_int(e)` and `const_string(e)` give `optional` values:
  `.hasValue()`, `.value()`, `.orValue(x)`.
- A message that interpolates an expression node shows its kind and place
  (`'Conversion at 7:43'`), not its text. Interpolate a name
  (`${cls(node).name}`) or a constant (`${const_string(e).orValue("?")}`).

## The file's keys

| Key | Meaning |
|---|---|
| `version` | `1` |
| `rules` | The list of rules. A file without `rules:` holds helpers only, for `uses:` |
| `defaults` | Keys given to every rule: `tier`, `family`, `since`, `status`, `severity`, `default_enabled`, `applies_to` |
| `define` | Helpers shared by the file's rules: a map of `name(params)` (or `NAME`, a constant) to an expression |
| `options` | Options every rule and helper of the file reads as `options.<name>` |
| `uses` | Helper files, relative to this file. A bare name with no such file beside it is a built-in one: `uvm.yaml` |
| `contexts` | Classifiers that label carriers before the rules run (experimental; below) |

## A rule's keys

| Key | Need | Meaning |
|---|---|---|
| `id` | required | 2-5 capitals, an optional `-TEAM` qualifier of 2-5 capitals, then 3 digits: `ACME001`, `ACME-DV001`. `SVCHK` is reserved |
| `name` | required | Unique kebab-case (`display-in-component`). Works anywhere an ID does: `--select`, `--explain`, the configuration |
| `tier` | required | Sets the default severity and the profiles the rule is in (below) |
| `summary` | required | One line, not interpolated |
| `message` | required | The finding's text, with `${expr}` interpolation (`$${` is a literal `${`). Say what is wrong and what to do |
| `match` | required | `kind`: one kind or a list (a class matches its subclasses). Optional `inside` / `not_inside`: `{ kind: K, stop: [K2] }`, an ancestor of kind K (searching up to the first `stop` kind) |
| `where` | optional | A bool expression on `node`; a finding when true. Default true |
| `let` | optional | Named expressions, in order |
| `define` | optional | Helpers of this rule only |
| `report` | optional | A list of nodes to report at instead of `node` (one finding each) |
| `options` | optional | The rule's options (below) |
| `applies_to` | optional | `{ axis: label }` or `{ axis: [labels] }`: run only in those contexts (experimental) |
| `examples` | optional | The rule's tests. Always write them |
| `doc` | optional | Rationale, escape hatch, sources (Markdown); `--explain` prints it |
| `family` | optional | One of `uvm`, `factory`, `class`, `decl`, `naming`, `expression`, `process`, `metric`, `slang` |
| `severity` | optional | `error`, `warning`, `info` or `hint`, over the tier's |
| `default_enabled` | optional | `false` keeps the rule out of its tier's profiles: it runs only when selected |
| `status` | optional | `active` (default), `deprecated`, `retired`; with `aliases` (former IDs) and `replaced_by` |
| `lrm` | optional | LRM clauses, as `"11.3.1.2"` |
| `since` | for `nursery` | The version the rule entered in, `YYYY.N` |

`x-...` keys are yours: the loader ignores them.

## Tiers

| Tier | Severity | For |
|---|---|---|
| `correctness` | warning | Code that is wrong: it fails, or does not do what it says |
| `suspicious` | warning | Code that is very likely wrong |
| `portability` | warning | Code that works in one simulator only |
| `simplify` | info | Code that could be simpler |
| `style` | hint | House style. A hint never fails a run, `-Werror` included |
| `pedantic` | warning | Strict checks with more false positives |
| `restriction` | warning | Bans of legal constructs a project chooses to forbid |
| `nursery` | warning | A rule still being proven; needs `since`. The built-in nursery rules run only when selected |

The built-in profiles (`--profile`) enable tiers; a `--rules` file's rules
run in every profile but `none`, so `--select` is not needed for them.

## Options

```yaml no-check
options:
  max_depth:   { type: int, default: 3, help: The deepest nesting accepted }
  allow_fork:  { type: bool, default: false, help: Accept fork/join_none }
  suffix:      { type: string, default: "_pkg", help: The package name suffix }
  style:       { type: string, default: lower, choices: [lower, upper], help: Name case }
  tasks:       { type: string-list, default: [$display], help: The tasks to report }
```

A rule reads `options.max_depth`. Users set them in a configuration, by ID
or name: `rule: { ACME001: { max_depth: 4 } }`. Examples run with the
defaults only.

## Examples

```yaml no-check
examples:
  - code: |                             # one file, example.sv
      module m; ... endmodule           // expect: ACME001
  - name: a near miss                   # names the example in failures
    code: |
      module m; ... endmodule
  - name: two files
    files:
      a_pkg.sv: |
        package a_pkg; ... endpackage   // expect: ACME001
      top.sv: |
        module top; import a_pkg::*; endmodule
```

- `// expect: ACME001` expects one finding of the rule on that line;
  `// expect: ACME001, ACME001` two; `// expect: ACME001 @ 4:17` one at
  that line and column of the example's text. `/* expect: ID */` works too.
- An example with no `expect` (or `// expect: none`) must report nothing.
- Each example compiles alone, with only its rule running, at the rule's
  default options. One that mentions `uvm_` compiles with `uvm_pkg` from
  `--uvm`: give it `import uvm_pkg::*;` and, for the macros,
  `` `include "uvm_macros.svh" ``.
- A wrong finding count is `SVCHK222`, listing what was expected where and
  what was reported where.

## Contexts and `applies_to` (experimental)

`uses: [uvm.yaml]` brings svcheck's UVM classifiers: each user package,
module, interface and class gets labels on the `unit` axis from what its
classes derive from (`agent`, `env`, `test`, `tb-top`, `seq-lib`,
`regmodel`, `base-lib`). A comment `// svcheck context: agent` before a
package overrides them. Then:

- `applies_to: { unit: agent }` runs the rule only where the innermost
  labelled carrier is an agent;
- `context(node, "unit")` is that unit (`null` if none):
  `"env" in context(node, "unit").labels`;
- `svcheck --rules FILE -f tb.f --contexts` prints every unit and the
  evidence for its labels, instead of findings.

A file can add its own classifiers:

```yaml no-check
contexts:
  - label: agent
    axis: unit
    carrier: Package            # Package, Definition or Class
    when: '!carrier.inLibrary && classes(carrier).exists(c, is(c, "uvm_agent"))'
    strength: strong            # content (default); weak = by name only, never selects
```

## Load diagnostics

| ID | Means |
|---|---|
| `SVCHK210` | Not an sv-rules file (no `sv-rules` key) |
| `SVCHK211` | A key the format does not define (the message suggests the right one) |
| `SVCHK212` | A field missing or of the wrong value: a bad ID, tier, option type, example |
| `SVCHK213` | `match` names an unknown node kind |
| `SVCHK214` | An expression is not valid CEL |
| `SVCHK215` | An expression does not type-check: an unknown field, function or option, a field read without narrowing. The message says which classes have the field |
| `SVCHK216` | An expression exceeds a limit (nesting, a recursive `define`, a bad regex) |
| `SVCHK217` | The ID or name is reserved (`SVCHK`) or taken, by another rule or a built-in one |
| `SVCHK218` | Not valid YAML for sv-rules; a leading `!` (a YAML tag) |
| `SVCHK219` | A rule's helper shadows a file helper of the same name |
| `SVCHK220` | The rule failed at run time (a read through null); it stops for that file |
| `SVCHK221` | The rule used up its evaluation budget for a file |
| `SVCHK222` | An example gave the wrong findings |
| `SVCHK223` | The rule names a type the design does not have |
| `SVCHK231` | The rule did not run: the model is missing (the design does not compile, or `--semantic=none`) |

`svcheck --explain SVCHK215` explains any of them.
