# svcheck

`svcheck` checks SystemVerilog, UVM testbenches first, against declarative
rules: `sv-rules` files, YAML with CEL expressions, run over
[slang](https://github.com/MikePopoloski/slang)'s syntax trees and over a
curated semantic model of the elaborated design.

- A built-in UVM pack (`SVCHK-UVM001`…`039`): construction, transactions,
  sequences, factory, phasing, config_db and component structure. Every rule
  is run on public corpora (OpenTitan, CORE-V-VERIF) before it ships.
- Presets for house styles where the UVM guides disagree: `uvm-consensus`,
  `easier-uvm`, `lowrisc-dv`, `core-v`.
- Your own rules in YAML, with firing and near-miss examples that
  `--check-rules` runs.
- Text, JSON, SARIF and GitHub annotation output; waivers in SV comments.
- A Python API that runs in-process: compile a bench and walk its model.

Documentation: <https://dvkit.org/fvutils/svcheck/>

## Install

```sh
pip install svcheck
```

Wheels are for Linux x86_64 (manylinux_2_28), CPython 3.10 to 3.14.

## Use

```sh
# The UVM pack, as the guides agree on it
svcheck --preset uvm-consensus -f tb.f

# Your own rules, as SARIF
svcheck --rules my-rules.yaml -f tb.f --format sarif > findings.sarif

# What a rule checks, and why
svcheck --explain SVCHK-UVM001
```

svcheck takes slang's command line (`-f`/`-F` file lists, `+incdir+`,
`+define+`, `--top`). UVM and code under `--library-dir` are compiled but not
checked. `--relax-lrm` accepts the LRM-strict errors that code developed on
other simulators often has.

```python
import svcheck
c = svcheck.compile(["-f", "tb.f"])
for pkg in c.design.packages():
    print(pkg.getName())
```

## Status

0.1 is the first release. The rule format, the UVM pack and the command line
are usable; `contexts:` and `applies_to:` (what kind of unit a rule applies
to: agent, env, test, ...) are experimental and may change.

## Building from source

```sh
ivpm update -d default-dev          # slang, fmt, rapidyaml, UVM, Python venv
uv pip install --python packages/python/bin/python --no-build-isolation -e .
packages/python/bin/python -m pytest
```

## License

Apache-2.0.
