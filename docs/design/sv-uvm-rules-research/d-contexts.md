# Research D: verification contexts

Raw research notes (web, 2026-10-16). Input to [../sv-contexts.md](../sv-contexts.md).
Anything that could not be checked against a primary source is marked **[unverified]**. The
Verification Academy Cookbook's "Agent" and "Dual Top Architecture" pages are members-only;
their public summaries and the original Mentor paper were used instead.

## 1. How methodologies split a testbench into contexts

**Siemens UVM Framework (UVMF)**
- Three generated unit kinds: interface packages, environment packages and project benches.
  Sequences come in the same three kinds.
  [VA UVMF track](https://verificationacademy.com/topics/uvm-universal-verification-methodology/uvmf/uvm-framework/),
  [MathWorks UVMF guide](https://www.mathworks.com/help/hdlverifier/ug/generate-and-integrate-test-bench-components-for-uvm-framework.html)
- Layout, from the generator output in Fitzpatrick & Jessiman, "How to Create a Complex
  Testbench in a Couple of Hours" (DVCon)
  ([PDF](https://dvcon-proceedings.org/wp-content/uploads/how-to-create-a-complex-testbench-in-a-couple-of-hours.pdf)):
  - interface package `mem_pkg/`: `mem_pkg.sv`, `mem_pkg_hdl.sv`, `mem_filelist_hdl.f`,
    `mem_filelist_hvl.f`; `src/` holds `mem_if.sv`, `mem_driver_bfm.sv`, `mem_monitor_bfm.sv`,
    `mem_typedefs_hdl.svh` and the classes `mem_agent.svh`, `mem_driver.svh` (proxy),
    `mem_monitor.svh`, `mem_configuration.svh`, `mem_transaction.svh`, `mem2reg_adapter.svh`;
  - environment package `<blk>_env_pkg/`: env, its configuration, sequence base, predictor;
  - bench `<blk>/tb/{testbench,tests,sequences,parameters}`, with `hdl_top.sv`, `hvl_top.sv`,
    `top_filelist_hdl.f`, `top_filelist_hvl.f`.
- The base library is split the same way: `uvmf_base_pkg_hdl` (synthesizable typedefs and
  parameters) and `uvmf_base_pkg` (classes). `hdl_top` holds the DUT, the BFMs and the
  signal interfaces. **[partially unverified: from summaries of the UVMF User's Guide]**
- BFM interfaces carry Veloce pragmas (`partition_interface_xif`, `xtf` tasks).
  [VA forum](https://verificationacademy.com/forums/uvm/uvmf-parametrized-hdl-typdefs)

**Split transactors / dual top.** van der Schoot, Saha, Garg, Suresh (Mentor), "Off To The
Races With Your Accelerated SystemVerilog Testbench", DVCon, about 2011 **[year unverified]**
([PDF](https://dvcon-proceedings.org/wp-content/uploads/off-to-the-races-with-your-accelerated-systemverilog-testbench-a-methodology-for-hardware-assisted-acceleration-of-systemverilog-testbenches.pdf)):
- two separate tops, `hdl_top` and `hvl_top`, with no cross-module or signal references
  between them;
- the HDL side is synthesizable (XRTL): DUT, clock and reset generators, BFMs; clock
  generators use `initial`, not `fork/join`;
- the HVL side is strictly untimed: no clock synchronisation, no `#`, no `wait`;
- BFMs are interfaces, not modules, reached through virtual interfaces; `_bfm` suffix; the pin
  interface is passed through the BFM's ports;
- no classes or other dynamic or unpacked types as BFM task/function arguments: packed fields,
  or converter classes with `to_class()`/`from_class()`;
- HVL-to-HDL calls may be time-consuming tasks; HDL-to-HVL calls are functions only, ideally
  void and one-way;
- a BFM may hold a back-pointer to its proxy and call its functions, but not touch its data
  members;
- BFMs are not factory-created or config-db-configured, only their proxies; HDL topology uses
  generate blocks and a shared parameter package;
- covergroups go in the HVL proxy, not in the BFM; assertions in BFMs are fine.
- Cookbook summary: the HDL top holds "everything associated directly with the clock
  cycle-based signal level activity".
  [VA](https://verificationacademy.com/cookbook/uvm-universal-verification-methodology/dual-top-architecture/)
- No Palladium-specific (Cadence) rules found. **[unverified]**

**lowRISC OpenTitan**
- Layers: `hw/dv/sv/dv_lib` (base RAL, agent, env classes); `hw/dv/sv/cip_lib` (Comportable
  IP: env config, RAL, scoreboard, virtual sequencer, base vseq, test); `hw/ip/<ip>/dv/env`.
  Agents in `hw/dv/sv/*_agent`, scaffolding from `uvmdvgen`.
  [Methodology](https://opentitan.org/book/doc/contributing/dv/methodology/index.html),
  [cip_lib](https://opentitan.org/book/hw/dv/sv/cip_lib/index.html)
- [DVCodingStyle.md](https://github.com/lowRISC/style-guides/blob/master/DVCodingStyle.md):
  packages `<blk>_agent_pkg`, `<dut>_env_pkg` (imports agent packages), `<dut>_test_pkg`
  (imports env packages); the testbench is module `tb` in `tb.sv` with DUT instance `dut`;
  files `` `include ``d into one package sit in its directory; drivers drive `X` on don't-care
  cycles; sequences avoid absolute delays (emulation); assertions are `bind`-ed from bind
  files; RTL uses `prim_assert` macros, DV code puts no side effects in `assert()`.
- Lint waivers are scoped per IP (`lint/<ip>.waiver`).
  [lint](https://opentitan.org/book/hw/lint/index.html)

**CORE-V-VERIF / Moore.io (Datum)**
- [CodingStyleGuidelines.md](https://github.com/openhwgroup/core-v-verif/blob/master/docs/CodingStyleGuidelines.md):
  a per-verification-kit prefix (unique, no underscore, 3–4 characters); files
  `<prefix>_pkg.sv`, `<prefix>_<name>_intf.sv`. Cross-context rule: "Code that resides in a
  testbench directory shall not use code from another testbench directory. Only code from
  vkit directories shall be visible to a testbench."
- Layer prefixes by usage, not by the guide: `uvma_` agent, `uvme_` env, `uvmt_` testbench,
  `uvml_` library. Layout `lib/uvm_agents`, `lib/uvm_libs`, `<core>/env/uvme`,
  `<core>/tb/uvmt`. [core-v-mcu-uvm](https://github.com/openhwgroup/core-v-mcu-uvm),
  [strategy](https://core-v-verif-verification-strategy.readthedocs.io/en/latest/corev_env.html)
- Moore.io agent template: `uvma_<x>` with `cfg`, `cntxt`, `mon`, `drv`, `sqr`, `if`,
  `seq_lib`, plus a self-test env `uvme_<x>_st` and testbench `uvmt_<x>_st`.
  [mio_agent_template](https://github.com/Datum-Technology-Corporation/mio_agent_template)
- No written rule found that an agent may not import a `uvme_` package; implied by the
  layering. **[unverified]**

**Accellera UVM / Verification Academy**
- Glossary: an agent is a signal interface, two BFM interfaces (driver, monitor) and a package
  of classes. [Glossary](https://verificationacademy.com/cookbook/uvm-universal-verification-methodology/glossary/)
- Agent config object holds the vif, `is_active` and protocol parameters; the test or env
  creates it and passes it down through `uvm_config_db`; a passive agent builds no driver or
  sequencer. [forum](https://verificationacademy.com/forums/t/configuring-agent-in-active-passive-mode/51489/6),
  [chipverify](https://chipverify.com/uvm/reusable-verification-components)

**Doulos Easier UVM**
([summary](https://www.doulos.com/knowhow/systemverilog/uvm/easier-uvm/easier-uvm-coding-guidelines/summary-of-the-easier-uvm-coding-guidelines/)):
every class in a package, one class per included file; `_pkg`, `_env`, `_agent` names;
`m_sequencer`, `m_driver`, `m_monitor` inside an agent; one agent per interface; driver and
sequencer only if `get_is_active`; an env works as top or sub-env; config flows top module →
test → env → sub-env/agent.

## 2. How SV tools scope rules

- **AMIQ Verissimo.** The ruleset XML's architecture configuration has
  `type="#COMPONENT_TYPE#"`, `base-class=`, `name-pattern=`; types include AGENT, DRIVER,
  MONITOR, SEQUENCER, SEQUENCE, SEQUENCE_ITEM, ENVIRONMENT, TEST, SCOREBOARD.
  [Ruleset syntax](https://eda.amiq.com/documentation/verissimo/toc/rulesets/ruleset-file-syntax-xml.html).
  Rules take properties, `<override-rule>` redefines them; `#SKIP_FILE_NAME_PATTERN#`,
  `#SKIP_ELEMENT_NAME_PATTERN#`. Pre-waivers skip analysis by path or module, interface,
  package or top (documented for third-party IP); post-waivers filter by check, path, message
  regex, line range.
  [Waivers](https://eda.amiq.com/documentation/verissimo/toc/lint-waivers/lint-waivers-file-syntax-xml.html),
  [third-party IP](https://eda.amiq.com/tutorials/verissimo-systemverilog-linter-how-to-skip-third-party-ips).
  Per-component-type parameters for one rule: **[unverified]**.
- **Sigasi.** Severity and parameters by path (`${rule id}/severity/${path}`; `@override` in
  `.sigasi/settings.json`), and by build target (`@targets`, e.g. ERROR under synthesis,
  IGNORE under simulation): the only RTL-vs-TB split found in a mainstream tool.
  [advanced settings](https://www.sigasi.com/tech/advanced-settings/),
  [linting](https://www.sigasi.com/manual/vscode/linting/)
- **Verible.** Per-directory `.rules.verible_lint` (`--rules_config_search`); waiver files and
  inline `verilog_lint: waive`; no structural role.
  [docs](https://chipsalliance.github.io/verible/verilog_lint.html)
- **svlint.** Nearest `.svlint.toml`; `[textrules]`, `[syntaxrules]`, `exclude_paths`.
  [OpenTitan's](https://github.com/lowRISC/opentitan/blob/master/.svlint.toml)
- **VC SpyGlass Lint, Questa Lint, Jasper Superlint.** RTL linters; methodology goals by
  standard (STARC, OpenMORE, DO-254, ISO 26262) or by IP/SoC/FPGA flow. No public evidence of
  classifying code as RTL, TB or VIP. **[unverified: docs behind logins]**
  [Synopsys](https://www.synopsys.com/verification/static-and-formal-verification/vc-spyglass/vc-spyglass-lint.html),
  [Siemens](https://www.siemens.com/en-us/products/ic/questa-one/design-solutions/lint/),
  [Cadence](https://www.cadence.com/en_US/home/tools/system-design-and-verification/formal-and-static-verification/jasper-verification-platform/jaspergold-superlint-app.html)

## 3. Analogues elsewhere: path, tag or derived role

- **ESLint flat config:** `files`/`ignores` globs, merged in order (path).
  [docs](https://eslint.org/docs/latest/use/configure/configuration-files)
- **eslint-plugin-boundaries:** `boundaries/elements` maps path patterns to element types with
  captures; `dependencies`/`element-types` rules allow or forbid from-type → to-type (derived
  from path). [repo](https://github.com/javierbrea/eslint-plugin-boundaries)
- **Nx:** project `tags`; `depConstraints` with `sourceTag` and `onlyDependOnLibsWithTags`
  (declared tag). [Nx](https://nx.dev/docs/features/enforce-module-boundaries)
- **dependency-cruiser:** `forbidden`/`allowed` with `from.path`/`to.path` regexes, captures
  reusable in `to` (path).
  [rules reference](https://github.com/sverweij/dependency-cruiser/blob/main/doc/rules-reference.md)
  (from memory)
- **ArchUnit:** `layeredArchitecture().layer("X").definedBy(...)` by package pattern or
  predicate (annotation, supertype); `whereLayer("X").mayOnlyBeAccessedByLayers(...)`
  (derived, can be structural). [user guide](https://www.archunit.org/userguide/html/000_Index.html)
- **Semgrep:** `paths: {include, exclude}`; `pattern-inside` and `metavariable-*` for
  structural context. [docs](https://semgrep.dev/docs/writing-rules/rule-syntax) (from memory)
- **clang-tidy:** nearest `.clang-tidy`, `InheritParentConfig` (directory).
  [man page](https://manpages.debian.org/experimental/clang-tidy-18/clang-tidy-18.1.en.html)
- **Android Lint:** test source sets (`checkTestSources`, `ignoreTestSources`); nested
  `lint.xml` (source set + path).
  [lint.xml](https://googlesamples.github.io/android-custom-lint-rules/usage/lintxml.md.html),
  [DSL](https://googlesamples.github.io/android-custom-lint-rules/usage/agp-dsl.md.html)
- **Rust/Clippy:** per-crate `[lints]` and `clippy.toml`; `cfg_attr(test, allow(...))`;
  `#[allow]` per item (crate, cfg, item). (from memory)
