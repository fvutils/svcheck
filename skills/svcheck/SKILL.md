---
name: svcheck
description: Run svcheck, the SystemVerilog and UVM checker built on slang, on a design or testbench and act on what it reports. Covers compiling a bench from its file list (UVM, defines, tops, simulator-only options, LRM relaxations), choosing rules (profiles, presets, --select/--ignore, the built-in UVM pack SVCHK-UVM, slang's warnings as SLANG markers), output formats (text, JSON, SARIF, GitHub annotations), exit codes, waivers, a project config file, CI use, the in-process Python API, and triaging findings with --explain. Use when the user wants to lint or check SV/UVM code, set svcheck up for a project or CI, understand or silence a finding, or pick a UVM house style. To write a new rule, use the svcheck-rules skill.
---

# Running svcheck

svcheck compiles SystemVerilog as slang does, runs declarative rules over
the result, and prints findings. Its command line is slang's (every source
option works) plus svcheck's own options for which rules run and how
findings are written.

Installed from PyPI (`pip install svcheck`), it puts `svcheck` on the PATH.
The same wheel has a Python API (below). `svcheck --help` lists every
option; `svcheck --version` prints the version.

## The loop

1. **Get a clean compile first.** Point svcheck at the bench's file list
   and fix what slang reports (below) before reading any finding. With
   compilation errors, rules that need the elaborated design are reported
   not run (`SVCHK231`), so a run with errors is not a clean bill.
2. **Choose the rules.** The default profile, `minimal`, runs almost nothing
   on its own; for a UVM bench ask for the UVM pack (`--select SVCHK-UVM`
   or a `--preset`).
3. **Run, read every finding**, and triage each one: fix the code, waive it
   with a reason, or turn the rule off for that code in the config.
4. **Pin the setup down** in a `--config` file so CI and people run the same
   thing.

## Compiling a bench

svcheck takes the same source arguments as slang: files, `-f`/`-F` file
lists, `+incdir+`, `+define+`, `--top`, `-G`, `--timescale`, `-y`/`-v`
libraries and so on.

```sh
svcheck -f tb.f --top tb_top
```

- **UVM is just source.** svcheck does not find UVM itself: the file list
  (or the command line) must compile `uvm_pkg.sv` with UVM's `src` on the
  include path, as for a simulator:
  `svcheck +incdir+$UVM_HOME/src $UVM_HOME/src/uvm_pkg.sv -f tb.f`.
  `uvm_pkg` is always library code: compiled, never checked.
- **Library code.** `--library-dir DIR` (repeatable) marks third-party code
  (vendor VIPs, generated RAL) as library: it is compiled for its types but
  gets no findings. slang's own warnings from it are silenced separately
  with slang's `--suppress-warnings "DIR/*"`.
- **Simulator options in the file list.** svcheck skips the common
  Verilator, VCS, Questa and Xcelium options (`-sverilog`, `-full64`,
  `-uvmhome`, `--cc`, ...) and maps `-top` to `--top`. Verilator's
  `--timing` collides with slang's own: add `--cmd-ignore --timing,0`.
- **No file list?** Write one: sources in dependency order (packages before
  their users), each include directory as `+incdir+DIR`.
- **`--top`.** With no `--top`, slang picks the uninstantiated modules. For
  a package-only compile (a VIP with no bench) the "no top-level modules"
  warning is harmless; `-Wno-missing-top` hides it.

### Code that simulators accept and slang rejects

slang follows the LRM strictly; benches often rely on what VCS, Questa,
Xcelium or Verilator let through. `--relax-lrm` turns on every relaxation
svcheck has (string scopes in `$assertoff`, `export pkg::*`, 2- vs 4-state
override types, string/integral compares, handles in `&&`, hierarchical
names in constants, ...). Try it first when a bench that simulates fails to
compile; `svcheck --help` lists the individual `--relax-*` / `--allow-*`
options if only one is wanted. Other slang escape hatches:
`--ignore-unknown-modules` (a missing DUT or macro model),
`--allow-use-before-declare`, `-Wno-error=<option>` (keep an error slang
lets be downgraded, such as `range-width-oob`, a warning).

## Choosing rules

What runs = the profile, then a preset, then the config, then the command
line; a later layer overrides an earlier one.

**Profiles** (`--profile`): `minimal` (default), `recommended`, `library`,
`strict`, `all`, `none`. A profile enables svcheck's own rules and markers
by tier. The rules of a file named with `--rules` run in every profile but
`none`. The UVM pack is in no default profile.

**The UVM pack** (`SVCHK-UVM001`...): built-in rules for construction,
factory use, phasing, objections, config_db, sequences, transactions and
reporting. `--select SVCHK-UVM` runs all of it; `--list-markers` lists it.

**Presets** (`--preset NAME`): the UVM pack as one house style sets it, with
that house's options, exclusions and naming conventions. Naming a preset
selects the pack.

| Preset | House style |
|---|---|
| `uvm-consensus` | Only the rules no source contradicts, at default options. A safe start for any bench. |
| `easier-uvm` | Doulos Easier UVM + Mentor cookbook: objections only in the test, virtual interfaces set for `uvm_test_top` only. |
| `lowrisc-dv` | lowRISC / OpenTitan DV methodology. |
| `core-v` | CORE-V-VERIF (OpenHW) conventions. |

Unsure which? Use `uvm-consensus`, or the preset matching the methodology
the bench was written to (its `uvma_*` / `*_agent_pkg` names usually say).

**Selectors** (`--select SEL`, `--ignore SEL`, repeatable): a rule ID
(`SVCHK-UVM037`) or name (`objection-in-agent`), an ID prefix
(`SVCHK-UVM0`), a namespace (`SVCHK-UVM`, `SLANG`), a tier (`correctness`,
`suspicious`, `style`, `pedantic`, ...) or `family:<name>`. The most
specific selector wins; at equal specificity an ignore beats a select.

**slang's warnings** are markers `SLANG001`... named as their slang options
(`SLANG177` is `constant-conversion`). None is in `minimal`; select them like
rules: `--select SLANG` or `--select constant-conversion`.

**Severity.** `-Werror` makes every warning an error, `-Werror=SEL` only
those a selector names (`-Werror=info` includes infos), `-Wno-error=SEL`
keeps some as warnings, `--no-warnings` drops warnings. Every other `-W`
option is slang's.

**Your own rules**: `--rules FILE` or `--rules DIR` (every rule file in it).
Writing them is the svcheck-rules skill.

Before a long run, check the selection without compiling anything:
`svcheck --preset easier-uvm --show-config` prints the resolved selection
as JSON, and `svcheck --select SVCHK-UVM --rules-for path/to/file.sv` the
rules a file gets.

## Output and exit status

`--format text` (default) prints `file:line:col: severity: message [ID]`
and a summary line. `--format json` is for scripts and agents: an object with
`findings` (each with `code`, `severity`, `message`, `file`, `line`, `col`,
`extent`, `context`), `waived`, `compilation` (the user files compiled) and
`rules` (each rule's tier). `--format sarif` is SARIF 2.1.0, for code-scanning
UIs. `--format github` writes GitHub Actions annotations.

Exit status: **0** no findings; **1** a finding of warning or error severity,
or a compilation with errors; **2** a rule file or configuration with errors,
or bad usage; **3** an internal error. Hints and infos alone exit 0.

`--statistics` prints the count per code (a first look at a big bench);
`--stats` the time and memory per phase.

When you run svcheck on a large bench, send its output to a file and read
the summary or the JSON, not the whole terminal stream:

```sh
svcheck -f tb.f --preset uvm-consensus --format json > svcheck.json 2> svcheck.log
```

## Triaging a finding

1. `svcheck --explain SVCHK-UVM037` (ID or name): the rule's tier, what it
   matches, its options and why it exists. Read it before deciding.
2. Decide which of these the finding is:
   - **Real**: fix the code.
   - **Deliberate here** (reviewed and accepted): waive it at the line,
     with a reason.
   - **Not this project's style**: set the rule's option, or ignore the rule,
     in the config (for all code or for some files or contexts).
   - **Wrong** (the rule misread correct code): report it to the svcheck
     project with a minimal example; waive it meanwhile.
3. Never silence a finding without saying why: a waiver without a reason,
   or one that waives nothing (`SVCHK202`), is itself reported.

### Waivers

A comment waives findings on its own line, or, alone on a line, on the next
line holding code. It names rules by any selector, then a reason:

```systemverilog
phase.raise_objection(this);  // svcheck waive SVCHK-UVM037: drain watchdog, reviewed
// svcheck waive objection-in-agent, SLANG177: reset sequence, reviewed
phase.raise_objection(this);
```

Waived findings are counted in the summary, listed under `waived` in the
JSON, printed with `--show-waived`, and written to SARIF as suppressed.

### Context labels

Some UVM rules apply only to some units (`applies to: unit: ["agent"]` in
`--explain`): svcheck classifies each package, module and interface by its
content (agent, env, test, tb-top, seq-lib, regmodel, base-lib). If such a
rule fires where it should not, or not where it should, check the
classification: `svcheck -f tb.f --select SVCHK-UVM --contexts` prints each
unit, its labels and the evidence. A preset adds its house's naming
conventions; where name and content disagree, `SVCHK-UVM039` reports it.

## A project config

`--config FILE` (YAML or JSON) holds what would otherwise be a long command
line. Paths and globs in it are relative to the file. The format is
provisional.

```yaml no-check
svcheck:
  preset: uvm-consensus
  rules: [lint/rules/]                    # the project's own rule files
  ignore: [style]
  severity: { SVCHK-UVM020: error }
  warnings: { error: [correctness] }      # these warnings fail the run
  rule:
    create-name-mismatch: { ignore_prefixes: [m_] }   # a rule's options, by name
  overrides:                              # in order; a later one wins
    - files: ["vendor/**", "gen/ral/**"]
      profile: none
    - context: agent                      # or "agent(apb)" for one unit
      ignore: [SVCHK-UVM036]
  contexts:                               # declare a label for some code
    - files: ["third_party/**"]
      context: vip
      axis: provenance
```

Run it with `svcheck --config svcheck.yaml -f tb.f`; options on the command
line override the file. `svcheck --config svcheck.yaml --show-config` shows
the result. A rule's options and their defaults are in its `--explain`.

## In CI

Run with the config, fail on what the project treats as errors, and give
the forge something it can display:

```sh
svcheck --config svcheck.yaml -f tb.f --format github      # GitHub Actions annotations
svcheck --config svcheck.yaml -f tb.f --format sarif > svcheck.sarif   # code scanning upload
```

Exit 1 fails the job on any warning. To adopt svcheck on an existing bench
without failing CI on day one: start from `uvm-consensus`, fix or waive what
it reports, then add rules (`--select`, a stricter preset or profile) a few
at a time. To report a rule without failing the job, lower it to `info` or
`hint` with `severity:` in the config: it is still printed, and exit 0.
(`--no-warnings` is applied before `-Werror=`, so it drops promoted warnings
too: it is not a way to keep only the promoted ones.)

## From Python

The wheel runs svcheck in-process, no subprocess:

```python
import svcheck

r = svcheck.lint(["-f", "tb.f", "--preset", "uvm-consensus"])
print(r.exit_status)
for f in r.report["findings"]:            # the --format json report
    print(f["file"], f["line"], f["code"], f["message"])

rc, out, err = svcheck.run_cli(["--explain", "SVCHK-UVM037"])   # any command line
```

`svcheck.compile(args)` returns the elaborated model itself (`c.design`, nodes
of `svcheck.sem`) for queries a rule cannot express: walk packages, classes,
members and calls directly. Its node kinds are those in the svcheck-rules
skill's model reference.

## Common problems

| Symptom | Cause and fix |
|---|---|
| UVM rules report nothing | The pack is not selected (`--select SVCHK-UVM` or a preset), or `uvm_pkg.sv` is not compiled, so nothing derives from `uvm_object`. |
| Many slang warnings from UVM or a VIP | slang reports them, not svcheck's rules: `--suppress-warnings "DIR/*"`. |
| `SVCHK231`, rules not run | slang reported errors; fix the compile (often `--relax-lrm`, a missing `+incdir+` or `+define+`, a missing file). |
| A rule-file error, exit 2 | A `--rules` file does not load; `svcheck --check-rules --rules FILE` gives the YAML line and column. |
| A finding in vendor code | Mark it library (`--library-dir`) or turn rules off for it (`overrides:` with `profile: none`). |
| An agent-only rule fires on an env, or the reverse | Check the classification with `--contexts`. |
