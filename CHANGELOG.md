# Changelog

## 0.1.0 — 2026-10-21

The first release.

- **Rules as data.** `sv-rules` files: YAML with CEL predicates over slang's
  syntax trees and a curated semantic model of the elaborated design. Rules
  carry firing and near-miss examples; `--check-rules` runs them.
- **The UVM pack**, `SVCHK-UVM001`…`039`, built in: construction,
  transactions, sequences, language, factory, phasing, config_db and
  components. Each rule was run on the public corpora (OpenTitan DV IPs,
  CORE-V-VERIF CV32E40P) before it shipped.
- **Presets** for house styles: `uvm-consensus`, `easier-uvm`, `lowrisc-dv`,
  `core-v` (`--preset NAME`).
- **The command line:** profiles and selectors (`--profile`, `--select`,
  `--ignore`, `-Werror=SEL`), `--config` with per-file overrides, waivers in
  SV comments (`// svcheck waive ID: reason`), `--explain`, `--rules-for`,
  `--show-config`, and text, JSON, SARIF and GitHub output. slang's warnings
  are reported as `SLANG###`.
- **`--relax-lrm`** accepts the LRM-strict errors that code developed on
  other simulators often has (local slang patches).
- **Python, in-process:** `svcheck.compile()` returns the model
  (`svcheck.sem`), `svcheck.lint()` the findings, `svcheck.run_cli()` the
  command.
- **Agent skills**, shipped in the wheel as `agent.skills` entry points:
  `svcheck`, for running svcheck (compiling a bench, choosing rules and
  presets, output, waivers, configuration, CI, triage), and `svcheck-rules`,
  for writing rules from a description.

Experimental: `contexts:` and `applies_to:` (unit classification: agent, env,
test, ...) and the `--config` format may change.

Wheels: Linux x86_64 (manylinux_2_28), CPython 3.10–3.14.
