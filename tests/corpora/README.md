# Corpora

File lists for the testbenches svcheck is tried on (svcheck-plan PR-5, P1.6b, P1.7). Each
`<name>.f` is read with `-F`, so its paths are relative to this directory. They name a
snapshot in `packages/<name>`, which is local only (`packages/` is gitignored) and is not an
ivpm dependency: the sources come from local checkouts or a generator. A snapshot is a git
repository whose first commits are the copy and whose later commits are svcheck's fixes;
the fixes are also kept here, as `<name>/fixes.patch`, so the snapshot can be rebuilt.

```sh
svcheck --rules rules/trial/contexts.yaml -F tests/corpora/fw-wb-dma.f
scripts/ctx_trial.py            # the contexts trial on all three
```

`tests/host/test_ctx_trial.py` checks that each one compiles without errors, and skips one
whose snapshot is missing.

| Corpus | What | Snapshot from | Fixes |
|---|---|---|---|
| `fw-wb-dma` | featherweight-ip's wb_dma UVM bench, wb flavour: 180 files | the working tree of `featherweight-ip/fw-wb-dma` (at `45cbb7b`, with uncommitted changes), its `packages/` deps and two generated `rundir/` directories | a dependency the env no longer imported, and LRM errors that Verilator lets through (P1-24): see below |
| `uvm-tb-proto` | uvm-tb-proto's generated SPI-controller bench (`out/`), with the VIPs it targets: 69 files | `uvm-tb-proto/out`; fwvip-common, fwvip-wb and fwvip-spi from `featherweight-vip/fwvip-spi` (its `packages/` and `src/`); fwprotocol-defs from `featherweight-vip/fwvip-wb/packages.1` (the one copy with `WB_MONITOR_PORT`) | the generated code did not match the fwvip APIs: interface ports `clock`/`reset`, transaction fields `we`/`adr`/`dat` |
| `opentitan-uart` | OpenTitan's UART DV, `lowrisc:dv:uart_sim` target `sim`, at opentitan `83fc48ed3a`: 377 files | FuseSoC 2.4.5's export of the target (generated RAL included), and UVM 1.2 (`fvutils/uvm-1.2/src`), which OpenTitan needs | none; `-Wno-string-int-concat` in the list (4 byte-in-string concatenations, which slang rejects by §11.4.12.2 and VCS accepts). Needed slang patch SL-2 |

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

`opentitan-uart` (`OT` an opentitan checkout; a venv with `fusesoc==2.4.5`, `hjson`, `mako`,
`pyyaml`, `tabulate`, `semantic_version`, `mistletoe`, `systemrdl-compiler`, `peakrdl-systemrdl`
and `pycryptodome`, which its generators import):

```sh
fusesoc --cores-root $OT run --target=sim --setup --build-root=/tmp/ot --tool=vcs lowrisc:dv:uart_sim:0.1
D=packages/opentitan-uart; mkdir -p $D
rsync -a /tmp/ot/lowrisc_dv_uart_sim_0.1/sim-vcs/src /tmp/ot/lowrisc_dv_uart_sim_0.1/sim-vcs/lowrisc_dv_uart_sim_0.1.eda.yml $D/
rsync -a <fvutils>/uvm-1.2/src/ $D/uvm-1.2/
```

`opentitan-uart.f` was written from the `.eda.yml`: each include file's directory as
`+incdir+`, each other file in order, `--top tb`.
