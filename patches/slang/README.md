# Local slang patches

svcheck builds slang from its release archive (`ivpm.yaml`, pinned to v11.0) with the patches
here applied by ivpm (`patches:` on the `slang` dependency, in both dep-sets). Slang issues are
tracked here rather than upstream for now (user, 2026-10-16). Each issue has a write-up, a
patch, and a repro that `tests/host/test_slang_patches.py` compiles, so a slang bump shows which
patches are still needed.

| ID | Issue | Patch | Repro | Upstream | Fixed upstream in |
|---|---|---|---|---|---|
| SL-1 | False "cyclic dependency" when a virtual interface type is compared with a cached duplicate instance's interface ([SL-1-param-cycle.md](SL-1-param-cycle.md)) | `0001-hasSameType-elaborate-bodies-first.patch` | `tests/host/slang/sl1-param-cycle.sv` | not reported | — (still in v12.0) |

## Adding one

1. Write the repro in `tests/host/slang/slN-<name>.sv` and add it to `test_slang_patches.py`.
2. Make the patch against the pinned release, `strip: 1` from slang's root, with a header naming
   the issue: `NNNN-<what-it-does>.patch`. Number in the order ivpm applies them.
3. Add it to `patches:` on both `slang` entries in `ivpm.yaml`, and a row here.
4. `ivpm update`, rebuild, run the tests.

## Bumping slang

Try each patch on the new release (`patch -p1 --dry-run`). Drop a patch whose repro passes
without it and record the release in the table; rebase one that no longer applies.
