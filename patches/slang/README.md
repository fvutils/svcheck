# Local slang patches

svcheck builds slang from its release archive (`ivpm.yaml`, pinned to v11.0) with the patches
here applied by ivpm (`patches:` on the `slang` dependency, in both dep-sets). Slang issues are
tracked here rather than upstream for now (user, 2026-10-16). Each issue has a write-up, a
patch, and a repro that `tests/host/test_slang_patches.py` compiles, so a slang bump shows which
patches are still needed.

| ID | Issue | Patch | Repro | Upstream | Fixed upstream in |
|---|---|---|---|---|---|
| SL-1 | False "cyclic dependency" when a virtual interface type is compared with a cached duplicate instance's interface ([SL-1-param-cycle.md](SL-1-param-cycle.md)) | `0001-hasSameType-elaborate-bodies-first.patch` | `tests/host/slang/sl1-param-cycle.sv` | not reported | — (still in v12.0) |
| SL-2 | "expected class property name" for an inherited property as a `randomize()` argument (§18.11 asks only for a property of the object). Found in OpenTitan's UART DV (P1.6b) | `0002-randomize-inherited-properties.patch` | `tests/host/slang/sl2-randomize-inherited.sv` | not reported | — |
| SL-3 | "macro operators may only be used within a macro definition" for two adjacent pastes, `a````b`: pasting the left token with the second `` keeps the `` in the expansion. Other tools paste once. Found in CORE-V-VERIF's CV32E40P bench (P2.1), which builds names around an empty argument | `0003-adjacent-macro-pastes.patch` | `tests/host/slang/sl3-adjacent-pastes.sv` | not reported | — (still on master, 2026-10-16) |
| SL-4 | "use of undeclared identifier" for a class's own type or enum value in a covergroup `sample()` argument that has a default: `MethodBuilder::copyArg` binds the default while the covergroup is still being built, before it is in the class. Found in CORE-V-VERIF (P2.1) | `0004-copied-default-values-bind-lazily.patch` | `tests/host/slang/sl4-covergroup-sample-default.sv` | not reported | — (still on master, 2026-10-16) |
| SL-5 | Errors that depend on parameter values, reported from a generic class body that slang checks with its parameters unset (for a class nothing specialises, and for every generic class of a package a wildcard-import lookup force-elaborates): `$readmemh` on an associative array indexed by `bit [XLEN-1:0]`, and a virtual interface `#(.W(W))` assigned from `virtual ifc`. Found in CORE-V-VERIF (P2.1) | `0005-generic-body-unset-parameters.patch` | `tests/host/slang/sl5-generic-body-unset.sv` | not reported | — (still on master, 2026-10-16) |
| SL-16 | Coverage built-in method names (`start`, `stop`, …) hid user names: a `sample()` argument named `start` bound to the built-in ("'void' is not a valid type for a coverage expression"), a bin named `start` was a "redefinition" and `binsof(cp.start)` found the method, and an implicit cross coverpoint named `start` likewise. Found in OpenTitan's i2c DV (P2.4b) | `0016-coverage-builtin-names.patch` | `tests/host/slang/sl16-coverage-builtin-names.sv` | not reported | — |
| SL-17 | "invalid use of non-static class property" for `binsof(cfg.vif.x)` of a hierarchical cross item (slang's own extension, `-Wnonstandard-hierarchical-cross`): `binsof` bound the name instead of matching the cross's item. Found in OpenTitan's sysrst_ctrl DV (P2.4b) | `0017-binsof-hierarchical-cross-item.patch` | `tests/host/slang/sl17-binsof-hier-cross-item.sv` | not reported | — |

SL-3 changes one of slang's own unit-test expectations: `PreprocessorTests.cpp:2871`
("Invalid macro concatenation regress -- GH #1484") expects a diagnostic that the patch no
longer gives. Everything else in slang's 2,298 unit tests passes with SL-1…SL-17 applied
(svcheck-plan P2.3 and P2.4b, with Catch2 v3.14).

### Relaxations (svcheck-plan P2.3, P2.4b)

LRM-strict errors that commercial simulators or Verilator let through, which real
testbenches depend on (P1-24, P2-2). slang is right by the LRM in each case, so these are
not bug fixes: each patch adds a `CompilationFlags` bit and a driver option, off by default,
which svcheck's command line passes through (and `--relax-lrm` turns on together). They
stack: each takes the next bit after the one before.

| ID | Error relaxed | Patch | Option | Repro |
|---|---|---|---|---|
| SL-6 | "expected scope or assertion name": `$assertoff(0, "tb.dut")`, a scope as a string (VCS; OpenTitan's `DV_ASSERT_CTRL`) | `0006-assertctl-string-scope.patch` | `--allow-assertctl-string-scope` | `sl6-assertctl-string-scope.sv` |
| SL-7 | "use of undeclared identifier": `export pkg::*` passes on only the names the package references (§26.6) | `0007-relax-package-exports.patch` | `--relax-package-exports` | `sl7-export-star.sv`, `sl7b-export-chains.sv` |
| SL-8 | `VirtualArgTypeMismatch`/`VirtualReturnMismatch` for `bit` against `logic` of the same width and sign (§8.20) | `0008-relax-virtual-method-types.patch` | `--relax-virtual-method-types` | `sl8-override-bit-logic.sv`, `sl8b-override-still-strict.sv` |
| SL-9 | `ForwardTypedefDoesNotMatch`: `typedef class X;` then `interface class X` (§6.18) | `0009-typedef-class-for-interface-class.patch` | `--allow-typedef-class-for-interface-class` | `sl9-typedef-iface-class.sv`, `sl9b-reverse-still-strict.sv` |
| SL-10 | `VirtualIfaceHierRef`: an interface with an upward reference used as a virtual interface (§25.9). Also (P2.4b) the copy slang builds for the virtual interface type is uninstantiated, so a name relative to a bind target is not reported there (the real instances check it) | `0010-virtual-iface-hier-ref.patch` | `--allow-virtual-iface-hier-ref` | `sl10-vif-upward-ref.sv`, `sl10b-vif-bind-target-ref.sv` |
| SL-11 | `NoImplicitConversion` between unpacked arrays whose elements differ in four-statedness or sign (§6.22.3); since P2.4b an output argument's copy-out too | `0011-relax-array-element-conversions.patch` | `--relax-array-element-conversions` | `sl11-queue-logic-bit.sv`, `sl11b-array-forms.sv`, `sl11c-output-queue.sv` |
| SL-12 | `BadBinaryExpression`: a string compared with an integral operand, `s[i] == t` (§6.16) | `0012-relax-string-compare.patch` | `--relax-string-compare` | `sl12-string-compare.sv` |
| SL-13 | `UsedBeforeDeclared`: a class method that returns a value, called without parentheses before its declaration | `0013-relax-forward-method-refs.patch` | `--relax-forward-method-refs` | `sl13-forward-method-ref.sv` |
| SL-14 | `BadBinaryExpression`: class handles as operands of `&&`, `\|\|`, `->`, `<->` (§8.4) | `0014-relax-logical-handle-operands.patch` | `--relax-logical-handle-operands` | `sl14-logical-handles.sv` |
| SL-15 | `a == b dist {…}` parsed as `a == (b dist {…})`: Table 11-2 against the `expression_or_dist` grammar. A parser option, not a compilation flag | `0015-relax-dist-precedence.patch` | `--relax-dist-precedence` | `sl15-dist-precedence.sv` |

`--relax-lrm` also turns on slang's own `--allow-hierarchical-const` (a hierarchical name in a
constant, as in a bind's parameter value).

SL-7 touches lookup: a failed wildcard-import lookup through a package with an export now
searches further, and when two exported packages hold the same name the first wins with no
ambiguity reported.

## Adding one

1. Write the repro in `tests/host/slang/slN-<name>.sv` and add it to `test_slang_patches.py`.
2. Make the patch against the pinned release, `strip: 1` from slang's root, with a header naming
   the issue: `NNNN-<what-it-does>.patch`. Number in the order ivpm applies them.
3. Add it to `patches:` on both `slang` entries in `ivpm.yaml`, and a row here.
4. `ivpm update`, rebuild, run the tests.

## Bumping slang

Try each patch on the new release (`patch -p1 --dry-run`). Drop a patch whose repro passes
without it and record the release in the table; rebase one that no longer applies.
Then append any new warning option to svcheck's SLANG numbering (`scripts/gen_slang_warnings.py
--update packages/slang/scripts/diagnostics.txt src/host/slang_warnings.txt`): the build fails
on an option the table lacks.
