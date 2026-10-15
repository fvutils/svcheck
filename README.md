# svcheck

`svcheck` checks SystemVerilog, and UVM testbenches first, against declarative
rules (`sv-rules`: YAML plus CEL), built on [slang](https://github.com/MikePopoloski/slang).

Status: bootstrap. Nothing is checked yet: the native tool compiles a design and
reports slang's diagnostics. See `docs/design/svcheck-design.md` for the design
and `docs/design/svcheck-plan.md` for the plan and its progress.

## Building

```sh
ivpm update -d default-dev          # slang, fmt, rapidyaml, UVM, Python venv
uv pip install --python packages/python/bin/python --no-build-isolation -e .
packages/python/bin/python -m pytest
```

or, native only:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/svcheck --dump-diags -F tests/data/uvm-2020.F
```
