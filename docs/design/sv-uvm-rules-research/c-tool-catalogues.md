# Research C: lint tools' testbench/UVM rule catalogues

Raw research notes (web, 2026-10-15). Input to [../sv-uvm-rules-assessment.md](../sv-uvm-rules-assessment.md).

# Testbench and UVM lint tools: built-in rules and how users add their own

## Bottom line

- **AMIQ Verissimo** has by far the largest public UVM catalogue. The full rule pool is only available by running `verissimo.sh -gen_rulepool_doc` (licensed). Its public changelog still names about 670 rules with IDs, and about 200 of those are UVM or testbench rules. Custom rules use a **Java API** whose documentation you get by emailing support.
- **Sigasi** has 10 UVM rules (IDs 145–154). You can configure them (severity and parameters) but you cannot write new ones.
- **Every open-source option is "write code"**: Python for draconian_uvm and the AsFigo tools, Rust dylib for svlint, and C++ for Verible and slang-tidy.
- **No declarative (YAML + query) rule format over an SV AST exists.** The closest precedent is ast-grep's YAML rules over a tree-sitter SV grammar loaded as a custom language. Semgrep and CodeQL have no SV support.
- **Commercial tools publish nothing at rule level for testbench code.** That covers Synopsys, Cadence, Siemens and Blue Pearl.

## 1. AMIQ DVT Verissimo (SystemVerilog Testbench Linter)

**Rule sets.** The predefined sets are `all`, `basic` (the default), `compliance` (UVM architecture), `dead_code`, `non_standard`, `performance`, `rtl` and `uvm_ieee` (removed, deprecated or non-standard 1800.2-2020 API).

**Rule ID families.** `SVTB.*` covers SystemVerilog testbench rules, `XVM.*` / `XVMnn` covers UVM/OVM, `R.nnnn` covers newer rules, and `UVM.*` is a legacy family.

**Configuration is an XML ruleset (`<ruleset version="2">`).** Features that matter for your format:
- **Categories** hold rules with `id`, `title`, `severity` and `description`, plus `description-append` and `value-append`.
- **Parameters** are set as `<property key=… value=…>`. Some rules are generic templates: `SVTB.7.14.0` "Build Calls Super" takes `baseClass=uvm_pkg::uvm_component` and `methodName=build_phase`.
- **Architecture model.** `<architecture><component type="DRIVER" base-class="uvm_driver" name-pattern=".*driver"/>` assigns roles. The roles are AGENT, DRIVER, MONITOR, SEQUENCER, SEQUENCE, SEQUENCE_ITEM, SCOREBOARD, ENVIRONMENT, TEST, CONFIGURATION_OBJECT, COVERAGE_COLLECTOR, ACTIVE/REACTIVE_COMPONENT and TESTBENCH. Defaults are things like `.*_drv`, `.*_scb` and `.*_item|.*_trans`.
  - The docs say name patterns exist so that a rule like "sequence items must inherit from uvm_sequence_item" is not a tautology: the pattern finds the candidates, then inheritance is checked.
- **Scoping and runtime controls:** `skip-file` and `skip-element` regexes, `<include labels=…/>`, `<include since="date"/>`, `<include file=…/>` with `override-configuration`, effort levels and timeouts per check, and git-blame annotations.
- **Waivers:** an XML waiver file, inline `// @DVT_LINTER_WAIVER …` comments with start/end ranges, and UVMF `// pragma uvmf … begin/end` regions. With the UVMF option, only the hand-written code inside those regions is linted.

**Custom rules.** The docs say only "Verissimo has a Java API that allows you to implement custom rules by querying the internal database… contact support@amiq.com". Some violations can be auto-corrected.

**Concrete UVM/testbench rules** (from the public changelog, titles verbatim but trimmed):

- **Factory**
  - XVM.2.1.1.5 registration macros must match the class name
  - XVM.2.1.1.6 factory registration macros for objects
  - XVM.2.1.1.4 registered classes must be package-qualified
  - XVM.2.1.1.2.1 no registration for classes with a protected/local constructor
  - XVM51b create calls must occur during phases
  - XVM.5.2.1.1 overrides must use compatible types
  - XVM.5.2.1.2 overrides before any create
  - XVM.2.8.5 set type overrides only in build_phase
  - R.1300 no factory overrides outside tests
  - R.1217 don't call uvm_factory override methods directly
  - R.1038 don't use global `uvm_pkg::factory`
  - XVM64 / XVM69 / R.1036 object, sequence and constructor-signature rules (empty-string default name)
- **Phases**
  - R.1204 overridden component phases must call super
  - R.1045 don't call super.build_phase when directly extending a UVM base class
  - XVM67 don't mix run_phase and sub-run phases
  - R.1043 don't override sub-run phases
  - R.1051 don't override pre_/post_ phase methods
  - R.1192 no reset/configure/main/shutdown phases in drivers, monitors or scoreboards
  - XVM65 call run_test() without arguments
  - R.1026 don't use xvm_root.run_test()
- **config_db**
  - XVM.4.21 / XVM.4.21.1 check get() return values
  - XVM.4.21.2 every get has a matching set
  - XVM.4.23 no wildcard/regex field names
  - XVM.4.26 no wildcard instance names
  - XVM.4.25 no null context
  - XVM.4.27 no whitespace in names
  - XVM.4.29 regexes must be valid
  - XVM.4.28 / R.1108 get/set only in build_phase or constructor
  - XVM.4.20 / R.1250 virtual interfaces go through config_db
  - R.1189 agent checks its vif was set
  - R.1252 top-module set uses "uvm_test_top" scope
  - R.1277 no absolute hierarchical path as the second argument to set()
  - R.1267 set only in tests and virtual sequences
  - R.1241 config-object field names contain `_config`
  - R.1271 don't use the resource DB
  - XVM.10.2 no `set_config_int/string/object`
- **Objections**
  - XVM70 pass a description string
  - R.1111 raised must be dropped / R.1115 dropped must be raised
  - R.1246 not in loops
  - R.1039 only in tests (XVM.2.8.9 says raise/drop in tests)
  - R.1061 in sequences only in pre_start/post_start
  - XVM.3.8 check starting_phase first
  - R.1055 propagate mode 0
- **Reporting**
  - UVM.5.1.3.6 / XVM.5.1.3.7 no `uvm_info with UVM_NONE
  - XVM.5.1.3.8 use the verbosity enum
  - XVM.5.1.3.9 guard message computation with a verbosity check
  - XVM.5.1.2 legal message IDs
  - R.1365 unique IDs
  - XVM.5.1.5 first argument contains a name or type name
  - XVM.5.1.4 no `uvm_error before start_of_simulation
  - XVM.5.1.6 / R.1407 no fatal in build
  - XVM.7.5 no report macros in final blocks
  - R.1176 / R.1410 no get_full_name(), get_name() or %m in reports
  - R.1409 report_enabled arguments must match the guarded call
- **Sequences and items**
  - XVM.3.6 / R.1201 randomize a sequence before starting it
  - R.1261 no `uvm_do* macros
  - R.1239 use `uvm_declare_p_sequencer
  - R.1272 `_vseq` suffix
  - R.1276 virtual sequences start on the virtual sequencer
  - R.1278 no items from virtual sequences
  - R.1138 no default_sequence
  - XVM.3.1.10 item fields not local/protected
  - XVM.3.5 no tasks in items
  - XVM.3.7 no 4-state in items
  - XVM.3.1.4.8 override convert2string
  - R.1013 do_pack/do_unpack
  - XVM66 do_compare must be overridden where called
  - XVM.3.9 do_kill if kill() is called
  - R.1121 set_item_context before randomize
- **Architecture**
  - XVM.2.6.3 agent has monitor, driver, sequencer and config
  - XVM.2.3.8 monitor must not assign interface members
  - R.1332 signal driving only in drivers
  - R.1149 interfaces accessed only in drivers/monitors
  - SVTB.7.35 class signal access through clocking blocks
  - XVM.2.2.10 drivers use NBA to clocking signals
  - XVM.2.2.3.1 driver has no analysis port
  - XVM.2.2.11 driver has one interface and one sequencer
  - XVM.2.1.18 no driver/monitor/scoreboard handles to each other
  - XVM.2.9.2.7 no tasks or vifs in scoreboards
  - XVM.2.9.2.8 don't modify received items
  - XVM.2.3.9 no register handles in scoreboards/monitors
  - XVM15c driver calls accept_tr/begin_tr/end_tr
  - XVM71 all TLM ports connected
  - XVM.5.4.5 no null/empty TLM port names
  - R.1289 use `uvm_analysis_imp_decl
  - R.1244 no explicit delays outside drivers/monitors
  - R.1253 no variable delays in drivers
  - R.1336 print topology in tests
- **Fields and API**
  - XVM.2.1.1.3 field macros match field type
  - XVM.7.6 no field macros in components
  - SVTB.7.1.1.1 field declaration order matches macro order
  - XVM.7.7 no undocumented API
  - R.1035 no `m_sequence`
  - R.1025 no `uvm_top`
  - XVM.10.1 no `uvm_transaction`
  - R.1000 / R.1032 UVM IEEE compliance and deviations
  - XVM.9.2.9 include uvm_macros.svh only at global scope
  - R.1194 no plusargs containing "uvm"
- **RAL**
  - XVM.4.22 check register-API status
  - XVM.4.24 no update() after predict()
  - R.1366 update() after set()
  - R.1367 no write/poke after set() without update()
  - R.1304 explicit prediction
  - R.1329 backdoor needs an HDL path
  - R.1320 no backdoor right after frontdoor
  - R.1187 build the register block in build_phase
- **Plain SystemVerilog testbench rules**
  - SVTB.15.4.1.4 don't assign randomize() status to the randomized variable
  - SVTB.15.8.1 shadowing inside randomize() with
  - SVTB.15.6.1 .sum() type mismatch
  - SVTB.15.9 no class-field functions in constraints
  - SVTB.7.33 don't override constraints
  - SVTB.7.29 / 7.31 randomizable means public
  - R.1288 no randomize-with
  - R.1129 no fractional weights
  - R.1340 / R.1327 constraint precedence
  - R.1402 / R.1411 no concatenations in dist or constraint sets
  - R.1416 initialize object arrays before randomize
  - SVTB.16.9.2 / 16.9.3 sample() versus a clocking event
  - XVM.2.9.1.5.1 build covergroups in the constructor
  - R.1008 no covergroups in items or sequences
  - SVTB.16.11 no option.weight or option.goal
  - R.1347 per_instance
  - R.1012 at_least ≠ 1
  - SVTB.16.4 no illegal_bins
  - SVTB.16.12 no implicit coverpoints
  - R.1355 unreachable bins
  - SVTB.8.4.6 / 8.4.7 / 8.4.8 / 8.4.9 / 8.4.10 / 8.4.11 fork/disable/wait-fork hazards
  - SVTB.13.2.1 wait(ev.triggered) instead of @ev
  - R.1344 no function calls in wait or event control
  - R.1034 waiting on a variable set in a parallel thread
  - SVTB.7.13.4 don't override non-virtual methods
  - SVTB.7.36 members protected/local
  - SVTB.21.6 no import at $unit scope
  - R.1426 #delay without timeunit

## 2. Sigasi Visual HDL

UVM linting is off by default and must be enabled. The UVM rules are:

| ID | Title | What it checks |
|---|---|---|
| 145 | Unregistered UVM object | Class not registered with a utils macro. Parameter `allow_unregistered_virtual_uvm_objects`. |
| 146 | Incorrect utility macro | Must use the right variant: component, object, param, or abstract (UVM 2017+). |
| 147 | Type argument value does not match containing class | Applies to the utils macros plus `uvm_field_utils_begin`, `uvm_sequence_library_utils`, `uvm_add_to_seq_lib`, `uvm_set_super_type`, `uvm_register_cb`. Parameters must be passed too. |
| 148 | Incorrect UVM object instantiation | Uses `new` instead of `type_id::create`. |
| 149 | UVM object name does not match variable name | |
| 150 | Unexpected output system task | `$display` and similar in UVM classes. Parameter `report_file_output_system_tasks` adds `$fdisplay`/`$fwrite`. |
| 151 | Incorrect override of UVM object | Original type must be a superclass of the override. |
| 152 | Deprecated UVM API | Parameter `up_to_version` = CURRENT, UVM_1_2, UVM_2017 or UVM_2020. |
| 153 | UVM phase method does not call superclass method | |
| 154 | Incorrect constructor for UVM object or component | Name/parent arguments; extra arguments optional. |

Other rules that matter for testbench code:
- 61: illegal class member access (local/protected)
- 62–68: overridden method signature mismatch
- 77: invalid randomize argument
- 80–81: constraint class scope
- 82–92: out-of-body method signature mismatch
- 129: prohibited macro (configurable list)
- 130: unused declaration
- 132: hidden non-virtual methods
- 162: cyclic class inheritance
- 183–184: non-standard cross / covergroup option

**Configuration.** Settings go in `.sigasi/settings.json`, e.g. `"verilog.rules.145.severity"` and `"verilog.rules.N.parameters.*"`, and can be scoped with `@override` per folder or file. Suppression is `// @suppress`. Sigasi also classifies code as RTL vs testbench, so severity can differ between them.

**Custom rules.** I found no mechanism, only parameters and naming-convention regexes.

## 3. Lightelligence draconian_uvm (duvm)

I cloned the repo and read its source. It is a Python, **line/regex-based** linter. Rules are `LineListener` subclasses on the internal `lw` (lintwork) broadcaster/listener framework. Files are sorted into TB-top (`_tb_top.`/`_sim_top.`), Test and UVC by filename regex, and each rule subscribes to the categories it applies to. Waivers are `// duvm: disable=Rule1,Rule2` and `enable=`.

| Rule | What it checks |
|---|---|
| AssertRandomize | `.randomize(` / `std::randomize(` must be wrapped in `` `cmn_assert``/`` `cmn_fassert`` |
| DollarDisplay | Bans `$display`; use `` `uvm_info`` |
| UvmMsg | Bans `` `uvm_info/warning/error/fatal`` in favour of `cmn_*` macros, plus verbosity checks |
| MisleadingSformat | `0x%d`-style base mismatch in `$sformatf` |
| CnstrSuffix | Constraint names end in `_cnstr` |
| CoverageSuffix | Covergroup names end in `_cg` |
| ClassnameSchema | Class suffix follows its base class: uvm_env→`env_c`, uvm_driver→`drv_c`, uvm_monitor→`mon_c`, uvm_scoreboard→`sb_c`, uvm_sequence_item→`item_c`, uvm_sequencer→`sqr_c`, uvm_sequence→`seq_c`, uvm_reg_adapter→`reg_adapter_c`, uvm_reg_block→`reg_block_c`, uvm_reg_cbs→`cb_c` |
| InstancenameSchema | `uvm_analysis_port`→`*ap`, `uvm_analysis_imp`→`*imp`, `uvm_event`→`*event` |
| UvmUtility | `` `uvm_component_utils`` vs `` `uvm_object_utils`` used in the right class kind |
| SuperFuncTask | `super.X()` must match the enclosing function or task name |
| ClassName / PkgName | Class, file, package and directory naming |
| MultipleClasses | One class per file, with exemptions for helper classes under 100 lines and `_seq/_vseq/_seq_lib` files |
| IncludeGuard | `` `ifndef __FILE__`` guard |

## 4. Verible and svlint

**Verible** rules that matter for testbench code (from the lint docs and source):
- `uvm-macro-semicolon`: no `;` after a `` `uvm_*`` call.
- `create-object-name-match`: the `type_id::create()` name matches the variable.
- `forbidden-macro`: the built-in list is just `` `uvm_warning``.
- `void-cast`: forbids `void'(…randomize…)` and `void'(uvm_hdl_read(…))`.
- `invalid-system-task-function`: `$psprintf`, `$random`, `$dist_*`, `$srandom`.
- `plusarg-assignment`: use `$value$plusargs`, not `$test$plusargs`.
- `constraint-name-style`: default `([a-z0-9]+_)+c`.
- `disable-statement`: `disable label` on a fork; use `disable fork`.
- Also: `numeric-format-string-style`, `macro-name-style` (exempts `uvm_`), `explicit-function-lifetime` / `explicit-task-lifetime` (outside classes), `explicit-function-task-parameter-type`, `instance-shadowing`, `suspicious-semicolon`, `endif-comment`, `package-filename`, `banned-declared-name-patterns`.

Custom rules are **C++ only**, compiled in, using the CST matcher library modelled on Clang ASTMatchers. The external route is `verible-verilog-syntax --export_json` plus Python, which is what AsFigo's svck, cnstlint and fcovlint use.

**svlint** (cloned):
- **Rule split.** TextRules run line by line before preprocessing (there are 4: `header_copyright`, `style_directives`, `style_semicolon`, `style_textwidth`). SyntaxRules run over the sv-parser tree after preprocessing (there are 155). Config is TOML `[textrules]` / `[syntaxrules]`, with everything off by default.
- **Testbench preset.** `verifintent.toml` is the testbench-oriented set. Rules relevant to testbench code:
  - `action_block_with_side_effect`
  - `function_same_as_system_function`
  - `function_with_automatic`
  - `procedural_continuous_assignment`
  - `keyword_forbidden_priority/unique/unique0`
  - `enum_with_type`
  - `re_required_*` / `re_forbidden_*` pairs for class, task, function, program, sequence, property, assert, `var_class` and `var_classmethod`
- **Plugins.** `--plugin libfoo.so` loads a Rust dylib exporting a `Rule::{Text, Syntax}` vector via `pluginrules!`. A plugin implements `check`, `name`, `hint` and `reason`, and sees the whole TOML config.

## 5. Commercial tools (public information is very thin)

- **Synopsys VC SpyGlass Lint:** the product page positions it as RTL lint "without requiring test benches". It mentions customer-specific rule capture but publishes no testbench or UVM rules. I found nothing public on a VCS UVM linter.
- **Cadence:** search results say HAL is legacy and was replaced by Jasper SuperLint, which is RTL-focused. The "200+ checks… UVM-compliance rules, testbench performance, race conditions, generation constraints" claim comes from the **Specman Elite (e language)** datasheet, not SystemVerilog. Both Cadence pages returned **403**, so I saw this only through search snippets.
- **Siemens Questa Lint:** "fast check of your RTL, without waiting for a testbench". You can configure your own methodologies and goals. There is no UVM content, and Visualizer is a debugger, not a linter.
- **Blue Pearl:** HDL lint for VHDL/Verilog/SV with no UVM or testbench content found.

## 6. Open-source and academic tools

- **PySlint (AsFigo, pyslang-based).** The DVCon paper "Testbench Linting – open-source way" says rules are written in Python over pyslang. Its rule tables are images and weren't readable. The public repo now holds only the framework (`AsFigoLintRule.apply(tree)`, a TOML `R101={enabled,severity}`). The rule IDs are listed in the README:
  - Naming: NAME_INTF_SUFFIX, NAME_CLASS_SUFFIX, NAME_CNST_SUFFIX, NAME_CG/CP/CR/PROP/AST/ASM/COV_PREFIX
  - SVA: SVA_MISSING_FAIL_AB, SVA_MISSING_LABEL, SVA_MISSING_ENDLABEL, SVA_NO_PASS_AB
  - Class: CL_METHOD_NOT_EXTERN, CL_MISSING_ENDLABEL
  - Compatibility: COMPAT_SVA_NO_CONC_IN_FE, COMPAT_DPI_OLD_SPECSTR, COMPAT_CG_OPT_PI_CL, COMPAT_SVA_NO_DEGEN_CONSEQ
  - Performance: PERF_CG_TOO_MANY_CROSS, PERF_CG_NO_ABIN_W_DEF_CL
  - Constraints: FUNC_CNST_MISSING_CAST, FUNC_CNST_DIST_COL_EQ
  - Reuse: REUSE_NO_TDEF_IN_MOD, REUSE_CG_NO_ILBINS_CL, REUSE_NO_WILDC_AA_CL, REUSE_ONE_CL_PER_FILE, REUSE_ONE_MOD_PER_FILE
- **UVMLint (AsFigo).** A SaaS product on top of PySlint with "140+ rules". The rule list is not public.
- **cnstlint and fcovlint (AsFigo, Verible JSON + Python).** Both are constraint/coverage linters; the `_VLT` suffix marks Verilator-compatibility rules.
  - cnstlint: CNST_DIST_NO_SOLVE_BEFORE_VLT, CNST_NO_LOCATOR_IN_CONSTRAINT_VLT, AF_CNST_NO_SOFT_FOREACH, CNST_RAND_TWO_STATE_ONLY, CNST_RANDC_DYNAMIC_EXCLUSION_VLT, CNST_SUM_WITH_NARROW_NO_CAST_VLT, CNST_UNIQUE_LARGE_ARRAY_VLT, CNST_RAND_MODE_LEFTOVER_VLT, FUNC_CNST_WRONG_OPER_PRE, FUNC_CNST_DIST_COL_SL, and others.
  - fcovlint: AF_FCOV_REQUIRE_PER_INSTANCE, AF_FCOV_FUNC_NO_GOAL_IN_CODE, AF_FCOV_AVOID_ILLEGAL_BINS, AF_FCOV_PERF_NO_EDGE_IMPL_SAMP, AF_FCOV_PERF_USE_CR_ABIN_MAX, CG_NAMING, AF_FCOV_FUNC_NO_MERGE_INST, and others.
- **OpenUVM-Lint** (Varsha-art126): a 2026 Rust prototype with one rule, UVM001 (`Class::type_id` used before the class is fully declared, a Verilator portability issue).
- **slang warnings that matter for testbench code:**
  - Randomization: `randomize-var-shadow`, `constraint-func-cycle`, `constraint-solve-cycle`, `constraint-missing`, `nested-solve-before`
  - Processes: `fork-loop-var`, `ref-arg-in-fork-join`, `event-const`, `nonblocking-final`
  - Classes: `shadow-property`, `virtual-arg-name-mismatch`, `member-impl-not-found`, `unassigned-property`, `unused-class-method/property`, `unused-local-class-*`, `unused-constructor`
  - Results and formatting: `unused-result`, `pointless-void-cast`, `dynamic-cast-const`, `format-too-many-args`, `missing-format`, `format-empty-arg`, `finish-num`
  - Simulator compatibility (nonstandard constructs some tools accept): `nonstandard-string-concat`, `string-int-concat`, `string-in-constraint`, `nonstandard-dist`, `nonstandard-randomize`, `nonstandard-inside`, `nonstandard-sys-func`, `package-import-in-class`, `nonstandard-hierarchical-cross`, `cross-ident-in-binsof`, `dpi-spec`
- **slang-tidy:** checks are C++ and compiled in. The only testbench-relevant ones are CovergroupName, CoverpointName and CrossName.
- **Verilator warnings that matter for testbench code:** ENCAPSULATED, SUPERNFIRST, IGNOREDRETURN, CASTCONST, WAITCONST, LIFETIME (fork outliving its declaring process), STMTDLY, TIMESCALEMOD, IMPORTSTAR, VARHIDDEN, IMPLICITSTATIC, NORETURN, CONSTRAINTIGN, COVERIGN, UNSATCONSTR (runtime), FINALDLY.
- **UVMF:** a YAML-driven generator. Hand-written code sits in `// pragma uvmf custom … begin/end` regions, and Verissimo has a mode that lints only those regions. The Lightelligence fork's README shows `run_verible_lint.py` run after generation.
- **Not found:** anything named "uvm_linter" or "uvmlint" as an open repo, and any pyslang UVM checker beyond AsFigo's.

## 7. Declarative and query-based precedents

| Approach | What exists |
|---|---|
| Semgrep / CodeQL | No Verilog or SV support found. |
| ast-grep + tree-sitter | ast-grep YAML rules run over any tree-sitter grammar registered as a custom language (dylib in `sgconfig.yml`). Grammars exist: tree-sitter-verilog, and gmlarumbe/tree-sitter-systemverilog (IEEE 1800-2023). This is the closest existing precedent, but it is syntax only, with no types or class hierarchy. |
| Parameterised built-ins | Not query languages, but they cover much of the need: Verible CST matchers (C++), svlint `re_required_*`/`re_forbidden_*` regex options, and Sigasi or Verissimo parameters. Verissimo's generic templates (base class + method name) and its role model (base class or name pattern) are the most reusable ideas for your format. |

## 8. Distinct UVM/testbench rule ideas, tagged by tool

Tags: **V**=Verissimo, **S**=Sigasi, **D**=draconian_uvm, **P**=PySlint/AsFigo (incl. cnstlint, fcovlint), **Vb**=Verible, **SL**=svlint, **Sg**=slang (incl. slang-tidy), **Vl**=Verilator.

**Factory and registration**
1. UVM class not registered with the factory — S, V
2. Wrong utils-macro variant (component, object, param, abstract) — S, D, V
3. Utils-macro type argument ≠ containing class (including parameters) — S, V
4. `new` used instead of `type_id::create` — S, V
5. Name passed to `create()` ≠ variable name — S, Vb
6. Constructor signature wrong (name/parent, defaults, empty-string default) — S, V
7. Override type not a subclass of the original — S, V
8. Override issued after a create, outside build_phase, or outside tests — V
9. Override methods called directly on uvm_factory, or global `factory` used — V
10. Registered class not package-qualified or not in a package — V
11. Registered class has a local/protected constructor — V
12. Create called outside phases — V

**Phases**

13. Overridden phase method doesn't call super — S, V, D (`super.X` must match the enclosing method name)
14. Calling super.build_phase when directly extending a UVM base (the inverse rule) — V
15. run_phase mixed with sub-run phases, sub-run phases overridden, or pre_/post_ phase methods overridden — V
16. `run_test()` called with arguments, or `uvm_root.run_test` — V

**config_db**

17. `config_db::get` return value unchecked — V
18. `get` has no matching `set` — V
19. Wildcard, regex, whitespace or invalid-regex field or instance names; null context — V
20. config_db used outside build_phase or the constructor — V
21. Virtual interface not passed through config_db, or the agent never checks it — V
22. Top-module scope not `uvm_test_top`, or an absolute path given — V
23. Resource DB or `set_config_*` used — V
24. `set` outside tests and virtual sequences — V

**Objections**

25. No description string — V
26. Unbalanced raise/drop — V
27. Raise/drop in loops — V
28. Objections outside tests, or outside a sequence's pre_start/post_start — V
29. starting_phase not checked, or propagate mode not set — V

**Reporting**

30. `$display` and similar in UVM code — S, D, V
31. Banned macros (configurable list) — S, Vb, D
32. UVM_NONE or wrong verbosity in `uvm_info — V, D
33. Illegal or duplicate message IDs; ID missing the object name — V
34. Errors before start_of_simulation, or fatal in build — V
35. Report macros in final blocks — V
36. `get_full_name()` or `%m` in report messages — V
37. Message computation not guarded by a verbosity check — V
38. Format-string base or argument mismatches — D, Vb, Sg
39. `` `uvm_* `` macro call followed by `;` — Vb

**Sequences and items**

40. Sequence not randomized before `start()`; `set_item_context` missing — V
41. `` `uvm_do* `` used — V
42. `p_sequencer` declared without the macro — V
43. Virtual-sequence naming or start rules; items sent from a virtual sequence — V
44. Item fields local/protected; tasks or 4-state fields in items — V
45. Item missing convert2string, do_pack/do_unpack or do_compare (and do_kill where kill() is called) — V
46. default_sequence used — V

**Architecture**

47. Monitor drives the interface, or signals driven outside drivers — V
48. Interfaces accessed outside drivers/monitors, or not through clocking blocks — V
49. Driver doesn't use NBA on clocking signals — V
50. Agent composition incomplete; driver has an analysis port — V
51. Cross-handles between driver, monitor and scoreboard; scoreboard modifies received items — V
52. TLM port left unconnected, or created with a null name — V
53. Multiple imps without `` `uvm_analysis_imp_decl`` — V
54. Analysis-port and event instance suffixes — D
55. Delays outside drivers/monitors; `#delay` without a timeunit; `timescale` used inconsistently — V, Vl

**Field macros and API**

56. Field-macro type mismatch; field macros in components; field order ≠ macro order — V
57. Deprecated or removed UVM API, by version — S, V
58. Undocumented or internal API (`m_sequence`, `uvm_top`) — V
59. `uvm_macros.svh` included outside global scope — V
60. Plusarg misuse (`$test$plusargs`, strings containing "uvm") — Vb, V
61. `void'(uvm_hdl_read(...))` — Vb

**RAL**

62. Register-API status unchecked; update/predict/set ordering wrong; backdoor without HDL path — V

**Randomization and constraints**

63. `randomize()` result ignored or void-cast — D, Vb, Sg, Vl, V
64. Name shadowing inside `randomize() with` — V, Sg
65. Constraint naming convention — D, Vb, P
66. `.sum()` on a narrow type without a cast — P, V
67. dist `:=` vs `:/`; weights; concatenations in dist — P, V
68. Operator precedence in constraints — P, V
69. Function calls in constraints; solve-before cycles — V, Sg
70. Constraints overridden; extern constraint without a body — V, Sg
71. Randomizable members not public, or public members not rand — V
72. rand on a 4-state type — P, V
73. Nonstandard constraint syntax (strings, parenthesized dist) — Sg

**Coverage**

74. Covergroup, coverpoint and cross naming — D, P, Sg
75. Sampling: `sample()` vs a clocking event — V, P
76. per_instance missing; goal or weight set — V, P
77. illegal_bins used — V, P
78. Too many crosses, or auto_bin_max issues — P
79. Covergroups placed in items/sequences, or not built in the constructor — V
80. Unreachable bins; nonstandard cross items — V, Sg

**Processes**

81. disable-fork and wait-fork hazards; `disable` of a fork label — V, Vb
82. Loop variable or ref argument used inside fork join_none/join_any — Sg, V, Vl
83. Wait or event control on a constant or a function call — Vl, Sg, V
84. `@ev` vs `wait(ev.triggered)` — V

**Classes and OOP**

85. Non-virtual method hidden or overridden; override signature mismatch — S, V, Sg
86. Method declared but not implemented; extern style — P, Sg
87. Property shadows a base-class property — Sg
88. Unused or never-assigned class members — Sg, S, V
89. Encapsulation violations or required members — S, Vl, V
90. `super.new` not first — Vl

**Files, packages and naming**

91. One class per file; file/class/package naming — D, P, Vb
92. Include guards — D, V, Vb
93. Class suffix derived from its UVM base class — D, V, P, SL
94. Import at `$unit` scope, or inside a class — V, Vl, Sg
95. `$random`/`$psprintf` banned; nonstandard concatenations (simulator portability) — Vb, Sg

**Assertions**

96. Side effects in assertion action blocks — SL
97. SVA labels and pass/fail action blocks missing — P

## Sources read

- Verissimo docs (eda.amiq.com/documentation/verissimo):
  - [index](https://eda.amiq.com/documentation/verissimo/index.html)
  - [available-rules](https://eda.amiq.com/documentation/verissimo/toc/available-rules/index.html)
  - [predefined-rulesets](https://eda.amiq.com/documentation/verissimo/toc/rulesets/predefined-rulesets.html)
  - [ruleset-file-examples](https://eda.amiq.com/documentation/verissimo/toc/rulesets/ruleset-file-examples.html)
  - [ruleset-file-syntax-xml](https://eda.amiq.com/documentation/verissimo/toc/rulesets/ruleset-file-syntax-xml.html)
  - [api-for-writing-custom-rules](https://eda.amiq.com/documentation/verissimo/toc/api-for-writing-custom-rules/index.html)
  - [inline-lint-waivers](https://eda.amiq.com/documentation/verissimo/toc/lint-waivers/inline-lint-waivers.html)
  - [whats-new](https://eda.amiq.com/documentation/verissimo/toc/whats-new/index.html) (full changelog)
  - [product page](https://eda.amiq.com/Verissimo_SystemVerilog_Testbench_Linter.html)
- Sigasi:
  - [Verilog rules index](https://www.sigasi.com/manual/creating-editing-exploring-code/linting/verilog/)
  - each UVM rule page under `…/linting/verilog/verilog_uvm_*/`
  - `verilog_prohibited_macro`, `verilog_class_item_visibility`
- [github.com/Lightelligence/draconian_uvm](https://github.com/Lightelligence/draconian_uvm) (cloned)
- [github.com/AsFigo/PySlint](https://github.com/AsFigo/PySlint) (cloned); READMEs and rules of [cnstlint](https://github.com/AsFigo/cnstlint), [fcovlint](https://github.com/AsFigo/fcovlint), [svck](https://github.com/AsFigo/svck); [asfigo.com/products](https://asfigo.com/products)
- [DVCon paper "Testbench Linting – open-source way"](https://dvcon-proceedings.org/wp-content/uploads/92146.pdf)
- [DVCon India 2025 Verissimo poster](https://dvcon-proceedings.org/wp-content/uploads/P15-DVConIndia25_Final_PPT_4873.pdf) (no rule content)
- [github.com/dalance/svlint](https://github.com/dalance/svlint) (cloned: MANUAL.md, rulesets/verifintent.toml, src/linter.rs, CHANGELOG)
- [Verible lint rules](https://chipsalliance.github.io/verible/lint.html), plus Verible sources `forbidden-macro-rule.cc` and `void-cast-rule.cc`
- [slang warning reference](https://sv-lang.com/warning-ref.html); slang `tools/tidy/src` tree via GitHub API
- [Verilator warnings](https://verilator.org/guide/latest/warnings.html)
- [ast-grep custom languages](https://ast-grep.github.io/advanced/custom-language.html)
- [Synopsys VC SpyGlass Lint](https://www.synopsys.com/verification/static-and-formal-verification/vc-spyglass/vc-spyglass-lint.html); [Siemens Questa Lint](https://www.siemens.com/en-us/products/ic/questa-one/design-solutions/lint/)
- [Lightelligence/uvmf README](https://github.com/Lightelligence/uvmf); [OpenUVM-Lint README](https://github.com/Varsha-art126/OpenUVM-Lint)

## Not accessible or not public

- **Verissimo full rule pool:** licensed, generated only with `-gen_rulepool_doc`. The rule names above come from the public changelog.
- **Verissimo Java API documentation:** available only by contacting AMIQ.
- **PySlint paper rule tables:** images, not extractable.
- **UVMLint rule list:** not public.
- **Cadence pages:** the Specman datasheet and the community HAL thread returned 403, so the Cadence notes come from search snippets only.
- **VC SpyGlass, Questa Lint, Blue Pearl:** no public testbench or UVM rule lists.

