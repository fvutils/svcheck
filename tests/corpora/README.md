# Corpora

File lists for the testbenches svcheck is measured on (svcheck-plan PR-5, P1.6b, P1.7, P2.1).
`corpora.yaml` lists them, with where each comes from, the packages it needs, whether it counts
towards precision, and why it does not compile, where it does not. Each `<name>.f` is read with
`-F`, so its paths are relative to this directory.

```sh
ivpm update -d default-dev,corpora --force    # the public corpora (large, not in CI)
scripts/corpora/opentitan.py                  # export the OpenTitan IPs; writes opentitan-*.f
scripts/precision.py                          # each rule's precision on the corpora
scripts/measure.py                            # P1.7's tables (plan §6.1)
svcheck --rules rules/trial/contexts.yaml -F tests/corpora/fw-wb-dma.f
```

`tests/host/test_corpora.py` checks that each corpus compiles without errors (or still has
them, for one listed with an exception) and that the precision reviews in `tests/precision/`
are current; it skips a corpus whose packages are absent.

There are three kinds of source:
- **ivpm**: the `corpora` dep-set of `ivpm.yaml` fetches OpenTitan, CORE-V-VERIF, the CV32E40P
  RTL and svlib at fixed revisions, and applies svcheck's fixes to them as patches kept here
  (`<name>/fixes.patch`). OpenTitan's DV needs FuseSoC to run its generators first;
  `scripts/corpora/opentitan.py` exports each IP to `packages/opentitan-dv/<ip>` and writes its
  list. `scripts/corpora/corev.py` writes CORE-V's from the bench's own `.flist` files.
- **snapshot**: a local copy in `packages/<name>` that is not an ivpm dependency (its sources are
  local checkouts whose remotes are internal hosts, or a generator's output). A snapshot is a git
  repository whose first commit is the copy and whose later commits are svcheck's fixes, also kept
  here as `<name>/fixes.patch`, so it can be rebuilt (below).
- **repo**: sources in this directory (`ctx-positive/`).

| Corpus | Source | What | Fixes |
|---|---|---|---|
| `fw-wb-dma` | snapshot | featherweight-ip's wb_dma UVM bench, wb flavour: 180 files, from the working tree of `featherweight-ip/fw-wb-dma` (at `45cbb7b`, with uncommitted changes), its `packages/` deps and two generated `rundir/` directories | a dependency the env no longer imported, and LRM errors that Verilator lets through (P1-24): see below |
| `uvm-tb-proto` | snapshot | uvm-tb-proto's generated SPI-controller bench (`out/`), with the VIPs it targets: 69 files. From `uvm-tb-proto/out`; fwvip-common, fwvip-wb and fwvip-spi from `featherweight-vip/fwvip-spi` (its `packages/` and `src/`); fwprotocol-defs from `featherweight-vip/fwvip-wb/packages.1` (the one copy with `WB_MONITOR_PORT`) | the generated code did not match the fwvip APIs: interface ports `clock`/`reset`, transaction fields `we`/`adr`/`dat` |
| `fwvip` | snapshot | the fwvip VIPs of `uvm-tb-proto` alone, with no testbench, test or top: P1.7's S-1/S-2 case | none |
| `ctx-positive` | repo | a bench written so that the context rules fire (P1.7) | — |
| `opentitan-<ip>` | ivpm | OpenTitan's DV for an IP, `lowrisc:dv:<ip>_sim` target `sim`, at opentitan `83fc48ed3a`: 350–470 files. FuseSoC 2.4.5's export, generated RAL included, with dvsim's `--mapping` to the generic prims. Compiled as dvsim does: one unit, UVM 1.2, the sim cfg's `+define+`s (dvsim's common ones, the IP's, those of its first build mode) and its `sim_tops` (the bind modules) as more `--top`s (P2.4b). All 18 compile: uart, aon_timer, dma, rv_timer, usbdev, adc_ctrl, pattgen, hmac, and since P2.4b aes, csrng, edn, entropy_src, i2c, keymgr, kmac, spi_device, spi_host, sysrst_ctrl | `opentitan/fixes.patch`: a streaming width, override access (hmac, pattgen). In every list: `--relax-lrm` (slang patches SL-6…SL-15) and `-Wno-string-int-concat` (byte-in-string concatenations, which slang rejects by §11.4.12.2 and VCS accepts). `-Wno-nested-solve-before` in adc_ctrl's; errors slang lets be downgraded kept as warnings in entropy_src's (`range-width-oob`), keymgr's (`bad-procedural-force`) and sysrst_ctrl's (`nonstandard-hierarchical-cross`, `nested-solve-before`). Needed slang patches SL-2, SL-10, SL-11, SL-16, SL-17 |
| `core-v-cv32e40p` | ivpm | CORE-V-VERIF's CV32E40P UVM bench (`uvmt_cv32e40p_tb`) at `cv32e40p_v1.8.3`, with the cv32e40p RTL and svlib at the revisions its `ExternalRepos.mk` names: 263 files. One unit, as dsim compiles it; the makefiles' default defines, no ISS; UVM 1.2 | `core-v-cv32e40p/fixes.patch`: modport selection through a virtual interface, an interface with interface ports held as a virtual interface, `get_coverage()` parentheses. `-Wno-nested-solve-before` in the list. Needed slang patches SL-3, SL-4 and SL-5 |

P1.6b's `opentitan-uart` was a hand-made export in `packages/opentitan-uart`, without the
prim mapping (so it had the ASAP7 prims). Its list now reads `packages/opentitan-dv/uart`; the
old snapshot is no longer read and can be deleted.

## Rebuilding a snapshot

`fw-wb-dma` (`FW` is a featherweight-ip/fw-wb-dma checkout whose UVM build has run, so its
`rundir/` has the register model and the PSS-generated packages):

```sh
D=packages/fw-wb-dma; mkdir -p $D/rundir
rsync -a --exclude .git --exclude packages --exclude 'packages.*' --exclude rundir --exclude '*.md' $FW/ $D/
for p in fw-hdl fw-proto-wb fw-proto-gpio fwvip-wb fwvip-gpio; do
  rsync -a --exclude .git --exclude packages $FW/packages/$p $D/packages/; done
rsync -a $FW/rundir/fw-wb-dma.reg-model $FW/rundir/fw-wb-dma.pss.op-model-sv $D/rundir/
(cd $D && git init -q && git add -Af . && git commit -qm snapshot && git am ../../tests/corpora/fw-wb-dma/fixes.patch)
```

The fixes (`fw-wb-dma/fixes.patch`, two commits): the env imports `fw_proto_wb_spl_pkg`,
where `wb_proto_client_export` now lives; the tests package imports `pssc_reg_pkg` and
`wb_dma_c_pkg` itself, since `export` passes on only the names the exporter references
(§26.6); two override arguments are `logic`, as in the base method (§8.20); override argument
names match the base's (§8.20; 8 of them, in the env and in fw-hdl); and fw-hdl forward-declares
an interface class with `typedef interface class` (§6.18).

`uvm-tb-proto` (`TBP` an uvm-tb-proto checkout, `SPI` a featherweight-vip/fwvip-spi
checkout with its packages, `WB` featherweight-vip/fwvip-wb):

```sh
D=packages/uvm-tb-proto; mkdir -p $D/vip/fwvip-spi
rsync -a $TBP/out $D/
rsync -a --exclude .git $SPI/packages/fwvip-common $SPI/packages/fwvip-wb $D/vip/
rsync -a $SPI/src $D/vip/fwvip-spi/
rsync -a --exclude .git $WB/packages.1/fwprotocol-defs $D/vip/
(cd $D && git init -q && git add -Af . && git commit -qm snapshot && git am ../../tests/corpora/uvm-tb-proto/fixes.patch)
```
