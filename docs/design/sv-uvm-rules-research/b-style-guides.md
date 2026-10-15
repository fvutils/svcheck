# Research B: SV/UVM testbench style guides

Raw research notes (web, 2026-10-15). Input to [../sv-uvm-rules-assessment.md](../sv-uvm-rules-assessment.md).

I read the full text of 9 sources and pulled out **146 deduplicated rules**, each with every source that states it. No files were written; everything is below. One finding matters for rule design: the guides often contradict each other, so the obvious house-style rules need parameters rather than fixed values (see the conflicts at the end).

**What "Static info" means:**
- **SYN**: a single construct, checked locally. Sometimes the call target must be resolved (e.g. `uvm_config_db#(T)::get`).
- **LEX**: tokens, preprocessor, text or file layout (line format, include guards, filenames).
- **CTX**: a construct plus its enclosing context, such as the method name or the class's resolved base type ("a `randomize()` call inside a `uvm_driver` subclass").
- **DECL**: class or module structure: members, member order, method overrides, the inheritance chain.
- **NAME**: a regex on an identifier, given its kind or type (sometimes compared with another name, such as filename against class name).
- **XREF**: across files or compilation units, or the elaborated hierarchy.
- **FLOW**: dataflow or call ordering.
- **NSC**: not statically checkable (judgment or runtime behaviour).

**Source keys:**
- **LR-DV**: lowRISC DVCodingStyle.md. It has no rule IDs, so the IDs are mine: section + item number.
- **LR-V**: lowRISC VerilogCodingStyle.md, testbench-applicable parts only.
- **OT**: OpenTitan cip_lib README and the OpenTitan Verible lint config.
- **CVV**: CORE-V-VERIF CodingStyleGuidelines.md, using its own numbers.
- **EU**: Doulos "Easier UVM" summary. The IDs are mine: section + bullet number.
- **MG-SV / MG-UVM / MG-PKG**: Mentor/Siemens UVM Cookbook (2012) appendix, using its own numbers.
- **SVIO**: systemverilog.io style guide.
- **VER**: Verible lint rule names. Verible is a linter rather than a style guide, but it encodes Google's non-public SV style guide and shows what is already checkable.
- **MZ**: Zwolinski's "MISRA-like" testbench standard.

## 1. Naming

| ID | Rule | Sources | Static |
|---|---|---|---|
| LR-DV-NAME-T | Standard handle names by role: `<if>_vif`, `env`, `rand cfg`, `cov`, `m_<dut>_agent`, `driver`, `monitor`, `scoreboard`, `virtual_sequencer` | LR-DV; EU-LEX-7/8/9 differ (`m_sequencer`/`m_driver`/`m_monitor`, `_env`/`_agent` suffix, `m_config`); CVV 1.8.2 (instance = type minus `_c`) | NAME+DECL (member type → base class) |
| LR-DV-NAME-1 | Top TB file `tb.sv`, module `tb`, DUT instance `dut` | LR-DV; CVV 4.2.1 says `tb_top` / `<tb>_tb_top.sv` | LEX+NAME |
| LR-DV-NAME-2 | The `type_id::create("x",…)` name string equals the assigned variable | LR-DV-NAME-2/FAC-4, EU-COMP-8/TX-14/SEQ-12, MG-UVM 2.3, VER create-object-name-match | SYN |
| LR-DV-FAC-5 | Array element create: name decorated with its index (`$sformatf("arr[%0d]",i)`) | LR-DV | SYN (create inside foreach) |
| EU-LEX-4 | UVM instance-name strings only use `[A-Za-z0-9_]` | EU | SYN (string literal) |
| LR-DV-NAME-3 | Package-scope typedefs, enums, params, functions and macros carry a unique (package) prefix | LR-DV; CVV 2.2.1 (vkit prefix on files, packages, interfaces, macros) | NAME (vs package name) |
| CVV 2.2.2/2.2.3 | Kit prefix has no `_`; 3–4 characters recommended | CVV | NAME |
| LR-DV-NAME-5 | `m_` prefix on members: class-object members (LR-DV), all members (EU-LEX-6, EU-SEQ-3), private members, which must also be `local`/`protected` (MG-SV 2.2) | LR-DV, EU, MG-SV | NAME+DECL |
| LR-V-NAMING | lower_snake_case for classes, packages, interfaces, instances, variables, functions/tasks, block labels | LR-V, EU-LEX-2, CVV 1.3.1, SVIO, MG-SV 1.7 | NAME |
| LR-V-PARAM | Tunable params UpperCamelCase, constants ALL_CAPS or UpperCamelCase ≠ params and `define UPPER_CASE | LR-V, OT veriblelint cfg, VER parameter-name-style; CVV 1.3.2, EU-LEX-3 | NAME |
| LR-DV-ENUM | Enum type lower_snake with block name, `_e` suffix; values UpperCamelCase including the enum name ≠ values UPPER | LR-DV, LR-V, VER enum-name-style; EU-LEX-3, SVIO | NAME |
| LR-V-ENUM-2 | Enums must be typedef'd, no anonymous enums, explicit storage type | LR-V, VER typedef-enums / typedef-structs-unions | SYN |
| CVV 1.7.1 | Type suffixes: `_c` class, `_e`, `_t`, `_s`, `_pkg`, `_intf`, `_i`, `_vi`, `_cb` (clocking), `_mp` (modport), `_cnstr`, `_cg` | CVV; partial overlaps: LR-V (`_e`/`_t`), EU-LEX-13/14/15 (`_vif`, `_t`, `_pkg`), MG-SV 1.8 (`_t`, `_e`, `_h`, `_m`, `_cfg`, `_ap`, `_group`), SVIO (`_s`, `_u`, `_e`) | NAME (by declaration kind) |
| CVV 1.8.1 | UVM-derived class suffixes by base class: `_env_c`, `_agent_c`, `_drv_c`, `_mon_c`, `_sqr_c`, `_seq_c`, `_item_c`, `_vseq_c`, `_sb_c`, `_cb_c`… | CVV | NAME+DECL (base class) |
| CVV 1.8.4 | TLM instance suffixes `_port`, `_export`, `_imp`, `_socket` (MG: `_ap`) | CVV, EU-LEX-11/12, MG-SV 1.8 | NAME+DECL (member type) |
| IF-NAME | Interface name `_if` (VER, LR-DV vif table) ≠ `<prefix>_<name>_intf` (CVV 2.5.1) ≠ definition `_io`, instance `_if`, clocking block camelCase (SVIO) | VER interface-name-style, CVV, SVIO | NAME |
| LR-DV-RND-1 | Constraint names describe what they constrain and end in `_c` (≠ `_cnstr`) | LR-DV, VER constraint-name-style; CVV 1.7.1 | NAME |
| CVV 3.4.3 | Method arguments named `_leading_underscore` | CVV 1.3.4/3.4.3 | NAME |
| CVV 3.3.1 | No single-letter class property names | CVV | NAME |
| LR-DV-GMAC-1 | Macro names: lower_snake if they wrap UVM functionality, otherwise UPPER_SNAKE | LR-DV, CVV 1.3.3, SVIO (variant), VER macro-name-style (UPPER, with an exception for `uvm_`) | NAME (`define) |
| LR-V-MACRO | Global define prefixed `GROUP__NAME`; a local define is prefixed `_` | LR-V | NAME |
| LR-DV-GOTCHA-2a | Macro names prefixed with the file or package name, no collisions | LR-DV, CVV 2.6.1 | NAME + XREF (collisions) |
| LR-DV-DPI-1 | DPI exports prefixed `sv_dpi_`, imports `c_dpi_` | LR-DV | NAME (DPI declaration) |
| EU-CFG-5/6 | Config class `<component>_config`, config_db field `"config"` (CVV 5.1.2: kit cfg `cfg_c`) | EU, CVV | NAME |
| EU-REG-4 | Variable holding the register block is named `regmodel` | EU-REG-4/12 | NAME |
| EU-SEQ-9 | Use built-in `req`/`rsp` in sequences | EU | NAME |
| MG-UVM 11.1/11.2 | User plusargs must not start with `uvm_`/`UVM_`; use a company prefix | MG-UVM | SYN (string literal arg) |
| CVV 8.4.1 | resource_db scope string starts with `<prefix>_pkg` | CVV | SYN+NAME |
| CVV 8.2.2 | Time-valued int/real variables named with their unit | CVV | NAME (heuristic) |

## 2. Files, packages, includes, preprocessor

| ID | Rule | Sources | Static |
|---|---|---|---|
| LR-DV-DIR-1 | One class per file; filename matches the class | LR-DV, MG-SV 3.3, MG-UVM 1.2 (`.svh`), CVV 1.4.1 (`<prefix>_<class>.sv`), SVIO | LEX+NAME (file ↔ declaration) |
| LR-V-FILE | `.sv` = compilation unit, `.svh` = included; one module per file, named after it | LR-V, MG-SV 3.2, VER module-filename / one-module-per-file | LEX+NAME |
| MG-PKG-1 | Package named `*_pkg`, file `<pkg>.sv` | MG-PKG, EU-LEX-15, CVV 2.4.2, VER package-filename | LEX+NAME |
| LR-DV-DIR-2 | Class files are `include`d into the package file; the package file is the only place includes appear (no nested includes) | LR-DV, EU-STR-4, MG-PKG, CVV 2.4.7 | LEX/XREF |
| MG-UVM 1.1 | Classes defined in a package, not in a module or at file scope | MG-UVM 1.1, EU-STR-3, CVV 2.4.1 | CTX (parent scope) |
| MG-SV 4.4 | No code in `$unit`; put it in a package | MG-SV | CTX |
| LR-DV-DIR-3 | Agent, env and test packages and tb.sv include `dv_macros.svh` and `uvm_macros.svh` and `import uvm_pkg::*` | LR-DV, EU-STR-7 | LEX/SYN |
| EU-STR-6 | No wildcard import at compilation-unit scope (LR-V: wildcard import only of a same-IP package, in the module header or body) | EU, LR-V | CTX/XREF |
| CVV 2.4.5 | `uvm_pkg` is the only package a kit package imports | CVV | SYN |
| MG-PKG-3 | Imports at the head of the package; included class files must not import | MG-PKG | CTX/LEX |
| CVV 2.4.6 | No cyclic package dependencies | CVV, LR-V | XREF |
| MG-SV 3.5/3.7 | Include a file in one place only (except macro files); reference package contents by import, not by including source | MG-SV | XREF |
| LR-DV-DIR-4 | All files included into one package live in one directory | LR-DV, MG-PKG, MG-SV 3.6 | LEX/XREF |
| LR-DV-DIR-5 | Layout: `<block>_agent/{…,seq_lib/}`, `<dut>/dv/{env,tests,tb}`; `env/seq_lib/<dut>_vseq_list.sv` included by the env package; env pkg imports agent pkg; test pkg imports env pkg | LR-DV, OT uvmdvgen | LEX+XREF |
| CVV 1.6 | Include guards on class and interface files, not on package files; guard name = `__UPPER_FILENAME__` | CVV 1.6.1–1.6.3, EU-STR-5 | LEX |
| LR-DV-GOTCHA-2b | Global macros go in `*_macros.svh` with an `ifndef` guard per macro, macros only | LR-DV, CVV 2.6.2/2.6.3/2.6.5 | LEX |
| LR-DV-GOTCHA-2c | Local macros: defined at top, `undef` at end of file, no `ifndef` guard; no `include between define and undef | LR-DV, LR-V | LEX (preprocessor pairing) |
| CVV 2.8.1/2.8.4 | Each module, interface and package file includes the kit's `<prefix>_macros.sv` at its start | CVV | LEX+XREF |
| CVV 2.8.6 / 2.1.1 | Never include a kit's files directly; testbench dirs must not use another testbench dir's code | CVV | XREF (paths) |
| CVV 1.1.1 | Common file header with author, filename, description and an SPDX line | CVV, SVIO (banner + docstring) | LEX |
| LR-DV-SIM | Simulator-specific code wrapped in `` `ifdef VCS/INCA/XCELIUM `` | LR-DV | LEX (needs a list of simulator-specific constructs) |
| LR-V-PP | Branching preprocessor directives left-aligned; `` `endif `` comment names its condition | LR-V, VER endif-comment | LEX |
| CVV 8.1.1 | No `define for constants or enums; use parameters or enums | CVV, LR-DV-GMAC-2, LR-V | LEX (define body is a constant) |

## 3. Formatting and lexical (testbench-applicable)

| ID | Rule | Sources | Static |
|---|---|---|---|
| FMT-INDENT | Indent 2 (LR-V) / 3 (CVV 1.2.1) / 4 (SVIO) spaces; no tabs | LR-V, CVV 1.2.2, SVIO, VER no-tabs | LEX |
| FMT-LEN | Lines ≤ 100 characters | LR-V, SVIO, OT cfg, VER line-length | LEX |
| FMT-FILE | ASCII only, LF line endings, final newline, no trailing spaces | LR-V, VER posix-eof / no-trailing-spaces | LEX |
| FMT-BEGIN | begin/end on all blocks (CVV 1.10, SVIO, MG-SV 1.3) / unless the statement fits on one line (LR-V) | VER explicit-begin | SYN |
| FMT-ONE | One statement or declaration per line | EU-LEX-1, MG-SV 1.2, SVIO | LEX/SYN |
| FMT-ENDLBL | End labels on class, function, task, package and module, matching the name; ` : ` spacing | MG-SV 1.10, SVIO, VER mismatched-labels, LR-V Labels | SYN |
| FMT-ELSE | `end` / `else` on a new line (SVIO) ≠ `end else begin` (LR-V examples) | SVIO | LEX |
| FMT-CALLSP | No space between function or macro name and `(` | LR-V, SVIO | LEX |
| FMT-PAREN | Parenthesize to disambiguate precedence | LR-V, MG-SV 1.4, VER suggest-parentheses | SYN |
| CVV 3.5.1 | Calls always use `()`, even with no arguments | CVV | SYN (needs callee resolved as a method) |
| CVV 3.5.2 | More than 2–3 arguments: use named (keyword) arguments | CVV | SYN |
| CVV 1.3.5 | Consistent `this.` usage for members within a package | CVV 3.5.3 | CTX (consistency) |
| CVV 1.9 / 3.3.3 / 3.4.1 | NaturalDocs comments; every property and non-standard method documented | CVV | LEX+DECL |
| LR-DV-UMAC-1 | No trailing `;` after `` `uvm_* `` macro calls | LR-DV, CVV 2.6.7, VER uvm-macro-semicolon | LEX |
| LR-DV-GOTCHA-1 | Macro args and expression-valued bodies are fully parenthesized | LR-DV | LEX (macro body tokens) |
| LR-DV-GOTCHA-3 | Multi-statement macro bodies wrapped in begin/end | LR-DV | LEX |

## 4. Class structure, construction, factory

| ID | Rule | Sources | Static |
|---|---|---|---|
| LR-DV-CLS-1 | Factory registration macro after variable declarations ≠ as the first line of the class (EU-TX-3/SEQ-2/COMP-2); CVV 3.2.1 gives a full section order (typedefs → macros → config fields → TLM → fields → `new` + phases in order → other methods) | LR-DV, EU, CVV | DECL (member order) |
| LR-DV-FAC-1 | Every class registered; parameterized classes use `*_param_utils` | LR-DV, MG-UVM 2.1/3.1, CVV 8.4.3; EU-CFG-7 says do **not** register config classes | DECL |
| MG-UVM 3.1 | Don't use `` `uvm_sequence_utils `` or `` `uvm_sequencer_utils `` | MG-UVM | SYN |
| FIELD-MACROS | Use `uvm_field_*` only in sequence items, never in components, flag `UVM_DEFAULT`, inside `utils_begin/end` (LR-DV-CLS-1/2, UMAC-3) ≠ never use field macros (EU-TX-4, MG-UVM 3.3) ≠ `UVM_COMPONENT` flag for component config fields (CVV 4.4.7) | LR-DV, EU, MG, CVV | SYN+CTX |
| EU-TX-8 | Transactions override `convert2string`, `do_copy`, `do_compare`, `do_print`, `do_record` | EU, MG-UVM 3.3, CVV 8.3.1 (`convert2string` one-line) | DECL |
| EU-TX-12 | `do_*` overrides must not use the printer/comparer/packer policy argument | EU | FLOW (argument unused) |
| LR-DV-CLS-3 | Use `` `uvm_object_new `` / `` `uvm_component_new `` unless the constructor has extra logic | LR-DV | DECL |
| LR-DV-CLS-4 | Hand-written constructor: `super.new(name[,parent])` is the first statement | LR-DV, MG-UVM 1.3, EU-TX-7/COMP-5 | DECL |
| LR-DV-CLS-5 | No constructor arguments beyond name and parent | LR-DV, MG-UVM 1.4 | DECL |
| LR-DV-NEW | uvm_object constructor: `name=""` default; uvm_component: no defaults at all | LR-DV, EU-TX-7/SEQ-5/COMP-5 | DECL |
| LR-DV-CLS-6 | Objects created in `build_phase` (components) or at the start of `body()` (sequences) | LR-DV, EU-COMP-6 | CTX |
| LR-DV-CLS-7 | Overridden phase methods call `super.<phase>_phase` when the parent is user-defined; EU-COMP-11: do *not* call `super.build_phase` when extending a UVM base class directly | LR-DV, EU-COMP-10/11 | DECL (inheritance chain, is base in uvm_pkg) |
| LR-DV-CLS-8 | Transactions extend `uvm_sequence_item`; never `uvm_transaction` | LR-DV, EU-TX-1, CVV 8.5.1 | DECL |
| LR-DV-FAC-2 | Create all objects and components with `type_id::create`; TLM objects and covergroups use `new()` | LR-DV, EU-FAC-1/TX-13/SEQ-11/COMP-7, MG-UVM 2.1 | CTX (`new` on a uvm_object subclass) |
| LR-DV-FAC-3 | `create()` named on a concrete type, never a type parameter | LR-DV | DECL (is the identifier a type parameter) |
| LR-DV-FAC-6 | Component create parent argument is `this` | LR-DV, EU-COMP-9 | SYN |
| LR-DV-FAC-7 | Objects created outside a component include `` `gfn `` in the name | LR-DV | CTX |
| LR-DV-FAC-8 | Overrides are in place before the object is created | LR-DV | FLOW |
| EU-FAC-2/3 | Override with `T::type_id::set_type/inst_override`; get the factory via `uvm_factory::get()`, never the static `factory` | EU, CVV 8.5.4 | SYN |
| CVV 3.6.1 | Don't redeclare (shadow) a base-class variable in a derived class | CVV | DECL (inheritance) |
| VIRT-OVR | Overrides of virtual methods repeat `virtual` (CVV 3.6.2) ≠ do not add `virtual` when overriding UVM methods (EU-LEX-18) | CVV, EU | DECL |
| CVV 3.3.4 | `rand` members must not be `local` (MG-SV 2.2: no local or protected) | CVV, MG-SV | DECL |
| MG-SV 2.3 | Declare methods `extern` ≠ CVV 3.1.1: no class headers / prototypes | MG-SV, CVV | DECL |
| MG-UVM 2.2 | Import packages that define factory-registered classes | MG-UVM | XREF |
| MG-SV 4.8 | Don't rely on static-variable initialization order | MG-SV | FLOW |

## 5. Components, configuration, TLM, test

| ID | Rule | Sources | Static |
|---|---|---|---|
| LR-DV-UVM | Call `run_test()` with no argument | LR-DV | SYN |
| LR-DV-AGT-1 | Agent contains only a sequencer, driver, monitor and config | LR-DV, EU-STR-9 | DECL (member types) |
| LR-DV-AGT-2 | One agent per interface; sequencer and driver built only if `cfg.is_active` / `get_is_active()` | LR-DV, EU-STR-8, EU-CFG-15 | CTX |
| LR-DV-DRV-1 | Driver talks to one sequencer and one interface, and has no analysis ports | LR-DV | DECL |
| LR-DV-DRV-2 | Driver doesn't randomize items it receives | LR-DV | CTX (`randomize` in a uvm_driver subclass) |
| LR-DV-DRV-3 | Driver drives `X` in don't-care cycles | LR-DV | NSC |
| EU-CLK-7 | Driver pulls items with `try_*` methods | EU | CTX |
| EU-CLK-11 | Monitor never assigns interface signals | EU | CTX+FLOW (LHS through a vif) |
| EU-STIM-6 / MG-UVM 5.1 | Drivers, monitors and scoreboards implement only `run_phase`, no runtime sub-phases | EU, MG-UVM | DECL |
| MG-UVM 5.2/5.4 | Avoid `reset/configure/main/shutdown_phase` and user-defined phases ≠ EU-STIM-6/7 (use them in tests and envs); EU-STIM-9: never override `pre_*`/`post_*` phases | MG, EU | DECL |
| LR-DV-SB | Scoreboard buffers with `uvm_tlm_analysis_fifo` | LR-DV | DECL |
| EU-TLM-1 | TLM connections (and vif assignments) made in `connect_phase` | EU | CTX |
| EU-TLM-4 | Peer-to-peer port→export with no intervening FIFO | EU | DECL |
| LR-DV-CFG-1 | Use `uvm_config_db` only: no `uvm_resource_db`, no `set/get_config_*` | LR-DV, EU-CFG-1, MG-UVM 6.1, CVV 8.5.2 (but CVV 4.4/8.4 uses resource_db for vifs) | SYN |
| LR-DV-CFG-5 | Check `config_db::get` return and `uvm_fatal` on failure (EU-CFG-10: or apply a default) | LR-DV, EU-CFG-9 | SYN (call used as statement vs condition) |
| LR-DV-CFG-6 | No wildcards in the `field_name` argument of `config_db::set` | LR-DV | SYN (literal) |
| EU-CFG-13 | No absolute hierarchical path in `set` inst_name | EU | SYN (literal) |
| LR-DV-CFG-4 | Don't pass ints or strings through config_db; pass config objects | LR-DV, MG-UVM 6.2 | DECL (`T` of `uvm_config_db#(T)`) |
| LR-DV-CFG-2/3 | Config class (extends uvm_object) per env and agent; env cfg holds agent cfg | LR-DV, EU-CFG-2/3/4, CVV 5.1.1 | DECL |
| CVV 8.4.4 | Component config_db calls pass `this` | CVV | SYN+CTX |
| EU-CFG-8 | get/set config only in `build_phase` | EU | CTX |
| EU-DUT-3/5 | Virtual interface carried in the config object; agent checks it is non-null | EU, MG-UVM 6.4 (set from tb top) | CTX/FLOW |
| CVV 5.1.3 | Config class must not create or `new` the reg block | CVV | CTX |
| CVV 4.1.1 | One root test `base_test_c` in `tests/base_test.sv`; all tests derive from it | CVV, OT (tests extend `cip_base_test#(ENV_T,CFG_T)`) | DECL+XREF |
| CVV 4.3.6 | Base test has `virtual function randomize_cfg()` | CVV | DECL |
| EU-TEST-1 | Tests don't generate stimulus (≠ CVV 4.3.5: base test starts basic sequences) | EU | CTX |
| OT-CIP | IP env classes extend `cip_base_*` / `dv_base_*` with type parameters; vseqs extend `<ip>_base_vseq` | OT cip_lib | DECL |
| EU-REG-1 | Never edit generated register-model code | EU | NSC/XREF |
| LR-DV-RAL | Beware non-atomic `uvm_reg::set()` / `update()` in parallel threads | LR-DV | FLOW/NSC |

## 6. Sequences, objections, end of test

| ID | Rule | Sources | Static |
|---|---|---|---|
| LR-DV-UMAC-4 | No `` `uvm_do* ``, `` `uvm_send ``, `` `uvm_create ``, `` `uvm_rand_send ``; use `start_item` / randomize / `finish_item` | LR-DV, EU-SEQ-7/8, MG-UVM 4.2 | SYN |
| MG-UVM 4.1 | Start sequences with `seq.start(sqr)` | MG-UVM, EU-SEQ-15 | SYN |
| LR-DV-SEQ-4 | Randomize a sequence before starting it | LR-DV, EU-SEQ-13 | FLOW |
| LR-DV-SEQ-3 | Use `pre_start` / `post_start`, not `pre_body` / `post_body` | LR-DV, MG-UVM 4.3, EU-SEQ-6 | DECL |
| LR-DV-SEQ-2 | No absolute-time delays in sequences (MG: no time consumption at all) | LR-DV, MG-UVM 4.5 | CTX (`#` in a uvm_sequence subclass) |
| LR-DV-SEQ-6 | Virtual sequences start on null or on a virtual sequencer | LR-DV-SEQ-5/6, EU-STIM-1/2 | SYN/FLOW |
| MG-UVM 4.6 | Virtual sequences null-check sequencer handles | MG-UVM | FLOW |
| MG-UVM 4.4 | Sequence inputs `rand`, outputs non-rand | MG-UVM, EU-SEQ-4 | NSC/DECL |
| MG-SEQ | No `fork…join_any; disable fork` of child sequences in a sequence | MG cookbook (Sequences chapter) | CTX |
| EU-SEQ-17 | Use `` `uvm_declare_p_sequencer `` for sequencer access | EU | SYN |
| EU-SEQ-10 | Only sequences generate items, not components or ad-hoc classes | EU | CTX |
| CVV 8.5.3 | Don't use `starting_phase` for objections (EU-OBJ-5/6 do use it, null-checked) | CVV, EU | SYN |
| LR-DV-OBJ-1 | `raise/drop_objection` pass a description string | LR-DV, EU-OBJ-7 | SYN |
| OBJ-WHERE | Objections only in the test, not per transaction (MG-UVM 8.2) ≠ in monitor `phase_ready_to_end` (LR-DV) ≠ main phase only by default sequences (CVV 6.1.1–6.1.3) | MG, LR-DV, CVV, EU-OBJ-3 | CTX |
| EU-OBJ-2 | `set_propagate_mode(0)` on objections | EU | FLOW |
| LR-DV-OBJ-2 | Base test sets a timeout with `uvm_root::set_timeout` | LR-DV | XREF |
| LR-DV-DVMAC-7 | End-of-test fifo/queue-empty checks in `check_phase` | LR-DV, CVV 7.1.3 | CTX |
| MG-UVM 9.1 | Don't use UVM callbacks (EU-SEQ-16: only `pre_do` / `mid_do` / `post_do` for immutable sequences) | MG, EU | DECL |
| MG-UVM 10.1 | Copy a transaction before modifying it after a TLM handoff | MG-UVM | FLOW |

## 7. Messaging

| ID | Rule | Sources | Static |
|---|---|---|---|
| LR-DV-LOG-4 | Report only through `uvm_info/error/fatal` macros; no `$display`, no `uvm_report_*` | LR-DV, EU-MSG-1, MG-UVM 3.2 | SYN |
| LR-DV-LOG-5 | No `` `uvm_warning `` (MG-UVM 3.2 lists it as allowed) | LR-DV, VER forbidden-macro | SYN |
| LR-DV-LOG-6 | Report ID is `` `gfn `` or `` `gtn `` (EU: a static string or `get_type_name()`) | LR-DV, EU-MSG-2 | SYN |
| LR-DV-LOG-7 | No `UVM_NONE` / `UVM_FULL` verbosity (EU-MSG-4: default to high verbosity) | LR-DV | SYN |
| LR-DV-LOG-3 | Don't use `uvm_object::print()`; one-line logs | LR-DV, CVV 8.3.1 | SYN |
| LR-DV-DVMAC-1 | Use `` `gfn `` / `` `gtn `` / `` `gn `` instead of `get_full_name()` etc. | LR-DV | SYN |
| LR-DV-DVMAC-5 | Comparisons via `DV_CHECK_*` macros, not hand-written if + report | LR-DV | SYN (pattern) |

## 8. SV language: randomization, processes, types, coverage, SVA, DPI

| ID | Rule | Sources | Static |
|---|---|---|---|
| LR-DV-DVMAC-6 | Randomize only through the `DV_CHECK_*RANDOMIZE*_FATAL` macros | LR-DV | SYN |
| RND-CHECK | Check `randomize()` and `$cast` return values (fatal on failure) | CVV 5.1.7, MG-SV 4.1/4.2, EU-SEQ-14 | SYN |
| LR-DV-ASRT | Never `assert(x.randomize())` or put side effects in `assert()`; use `if` | LR-DV, MG-SV 4.3 | SYN (+ purity of callee = FLOW) |
| LR-DV-VOID | `void'()` only on system functions, RAL `predict`, fifo/queue methods; never on `randomize` or `uvm_hdl_read` | LR-DV, VER void-cast | DECL (callee resolution) |
| LR-DV-RND-2 | Avoid `foreach` in constraints; prefer `inside` | LR-DV | SYN |
| LR-DV-RND-3 | No arithmetic inside constraint loops | LR-DV | SYN |
| LR-DV-RND-4 | Bitmask instead of `%` in constraints | LR-DV | SYN |
| LR-DV-RND-5/6 | Split complex constraints; randomize array-of-object elements individually | LR-DV | NSC / SYN |
| CVV 8.5.5 | Never use `soft` or `default` constraints | CVV | SYN |
| CVV 5.1.8 | Avoid `$urandom*` / `$random` / `$dist_*` for members (unconstrainable) | CVV | SYN |
| LR-DV-SYS | Forbidden: `$psprintf`, `$random`, `$dist_*`, `$srandom` | LR-DV, VER invalid-system-task-function | SYN |
| VER plusarg | No `$test$plusargs`; read plusargs with a value | VER plusarg-assignment | SYN |
| LR-DV-FN | Functions and tasks in packages, modules and interfaces are explicitly `static` or `automatic` | LR-DV, VER explicit-function-lifetime / explicit-task-lifetime | SYN+CTX |
| VER fn-arg | Explicit types on all function and task arguments; package params explicitly typed | VER explicit-function-task-parameter-type, LR-DV-NAME-4 | SYN |
| LR-DV-AA | No wildcard `[*]` associative arrays | LR-DV, MG-SV 4.5 | SYN |
| LR-DV-LOOP | Prefer `foreach` over equivalent `for` loops | LR-DV | SYN (heuristic) |
| LR-DV-IF-1 | No `program` (MG also: no `checker`, no `final`) | LR-DV, EU-CLK-2, MG-SV 4.7 | SYN |
| LR-DV-IF-2 | Clocks and resets generated in modules or interfaces, never in uvm_object classes or programs | LR-DV, EU-CLK-1 | CTX |
| LR-DV-IF-3 | Use clocking blocks in interfaces for synchronous driving and sampling; modports enforce clocking-block use | LR-DV, EU-CLK-3/4/5 | DECL / FLOW (vif access path) |
| EU-CLK-12 | Protocol assertions and `cover property` live in interfaces | EU, EU-COV-2 | NSC/DECL |
| CVV 8.2.1 | No `` `timescale ``, `timeunit` or `timeprecision`; delays carry explicit units | CVV | SYN |
| MG-SV 4.6 | No `#0` delays | MG-SV, MZ 3.2/13.2 | SYN |
| LR-DV-FORK-1 | `join_any` + `disable fork` only inside an isolation fork; use `disable fork`, not disable-by-label | LR-DV, VER disable-statement | SYN |
| LR-DV-FORK-2 | Don't mix begin/end and bare branches in one fork; `fork begin` on one line only with a single branch | LR-DV | SYN |
| LR-DV-DVMAC-8 | Label purposeful forks (e.g. `isolation_fork`) | LR-DV | SYN |
| LR-DV-WAIT | Every `wait` and non-forever loop has a watchdog (`DV_WAIT` / `DV_SPINWAIT`) | LR-DV, MZ 5.2/9/13.7 | SYN (bare `wait`) / NSC |
| LR-DV-BIND | Prefer `bind` for assertion monitors; `.*` only if the bound module is inputs-only | LR-DV | SYN+DECL |
| LR-DV-SVA-1 | SVA checker module ports are all inputs | LR-DV | DECL |
| LR-DV-SVA-2 | Use `` `ASSERT `` macros (prim_assert), not hand-written `assert property` | LR-DV | SYN |
| LR-DV-SVA-3 | Bind SVA modules, don't instantiate them in tb | LR-DV | CTX/XREF |
| LR-DV-SVA-4 | No absolute hierarchical paths in SVA modules | LR-DV, LR-V (hier refs) | SYN (path root) |
| LR-DV-SVA-6/7 | Binds in a dedicated bindfile module; bind to a module type, not an instance path | LR-DV | CTX |
| LR-DV-BD | Chip-level force/probe targets ports, not internal nets or clocks | LR-DV | XREF (elaborated: is target a port?) |
| LR-DV-DPI-4 | DPI arguments of basic types only; don't use DPI where SV suffices | LR-DV | DECL / NSC |
| LR-DV-FC | Don't use `illegal_bins` as a check (EU-COV-17: never; use `ignore_bins`) | LR-DV, EU | SYN |
| COV-PLACE | Covergroups in components, not transactions or sequences (EU-COV-4/5) ≠ in uvm_object wrapper classes (MG-SV 4.9, MG-UVM 7.1); constructed in the constructor (EU-COV-6) | EU, MG | CTX |
| COV-SAMPLE | Sample via `.sample()`, not a clocking event; conditional (`iff` / `coverage_enable`) | EU-COV-11/12, MG-SV 4.10/4.11 | SYN |
| CVV 5.3.3/5.3.6 | Classes with coverage have `coverage_enable`; covergroup construction is conditional on it | CVV, EU-COV-10 | DECL+CTX |
| MG-SV 4.12/4.13 | Label coverpoints and crosses; name bins explicitly | MG-SV | SYN |
| EU-COV-15 | Don't set `option.weight` / `option.goal` | EU | SYN |
| LR-V-DECL | No implicit nets; everything declared | LR-V, MZ 13.4 | SYN (elab) |

**Coverage by category:** about 45% of rows are SYN or LEX, 20% NAME, 20% DECL or CTX (they need resolved class hierarchy and method context), and the rest are XREF, FLOW or NSC. MZ is mostly NSC; its only statically checkable rules (`#0`, implicit nets, unbounded waits) are already in the tables.

**Conflicts that need parameterized rules rather than fixed ones:**
- **Field macros:** LR-DV requires them; EU and MG forbid them.
- **`` `uvm_warning ``:** LR-DV forbids it; MG allows it.
- **Indentation:** 2 (LR-V), 3 (CVV) or 4 (SVIO) spaces.
- **Suffix schemes:** `_c`, `_intf` and `_cnstr` in CVV, versus `_if` and `_c` constraint names elsewhere.
- **Enum value case:** UpperCamelCase in LR-DV versus UPPER in EU and SVIO.
- **`virtual` on overrides:** CVV requires it; EU says leave it off.
- **Runtime phases:** MG says avoid them; EU uses them for stimulus.
- **Who raises objections:** the test only (MG), the monitor's `phase_ready_to_end` (LR-DV), or the default sequences (CVV).
- **Factory registration of config classes:** LR-DV registers all classes; EU says don't register config classes.
- **resource_db:** banned by LR-DV and EU, but CVV uses it to pass virtual interfaces.
- **`extern` methods:** MG recommends them; CVV says no class headers or prototypes.
- **`super.build_phase`:** LR-DV calls it when the parent is user-defined; EU says not to call it when extending a UVM base class directly.

## URLs read
- https://raw.githubusercontent.com/lowRISC/style-guides/master/DVCodingStyle.md (all of it)
- https://raw.githubusercontent.com/lowRISC/style-guides/master/VerilogCodingStyle.md (relevant sections)
- https://raw.githubusercontent.com/lowRISC/opentitan/master/doc/contributing/style_guides/README.md
- https://raw.githubusercontent.com/lowRISC/opentitan/master/doc/contributing/dv/methodology/README.md (process content only, no coding rules)
- https://raw.githubusercontent.com/lowRISC/opentitan/master/hw/dv/sv/dv_lib/README.md
- https://raw.githubusercontent.com/lowRISC/opentitan/master/hw/dv/sv/cip_lib/README.md
- https://raw.githubusercontent.com/lowRISC/opentitan/master/util/uvmdvgen/README.md
- https://raw.githubusercontent.com/lowRISC/opentitan/master/hw/lint/tools/veriblelint/lowrisc-styleguide.rules.verible_lint
- https://raw.githubusercontent.com/openhwgroup/core-v-verif/master/docs/CodingStyleGuidelines.md (all of it; derived from Brian Hunter's *Advanced UVM*)
- https://www.doulos.com/knowhow/systemverilog/uvm/easier-uvm/easier-uvm-coding-guidelines/summary-of-the-easier-uvm-coding-guidelines/
- https://iccircle.com/static/upload/img20240207093017.pdf — a third-party mirror of the Mentor UVM Cookbook (2012). I used the SV/Guidelines and UVM/Guidelines appendix, the package guidelines and the sequences chapter.
- https://www.systemverilog.io/verification/styleguide/
- https://chipsalliance.github.io/verible/lint.html, plus the Verible sources `forbidden-macro-rule.cc` and `void-cast-rule.cc`
- https://raw.githubusercontent.com/MarkZwolinski/SystemVerilog-Coding-Standards/main/Testbench-MISRA-Style-Guide.md and `Testbench-Linting-Guidance.md`
- https://www.uvmcollab.org/uvm/guidelines/coding-guidelines/ (index only: points to lowRISC, Doulos and Verification Academy)
- CONTRIBUTING.md files: pulp-platform/style-guidelines (archived, defers to lowRISC), openhwgroup/cva6 (adopts lowRISC plus verible-format), chipsalliance/caliptra-rtl and chipsalliance/Caliptra (no SV coding rules), lowRISC/ibex (no style content), chipsalliance/riscv-dv (only runs Verible)

## Inaccessible or not a source
- **Verification Academy online cookbook guidelines** (verificationacademy.com/cookbook/sv/guidelines and /uvm/guidelines): need a login. The Wayback Machine returned 429. I used the 2012 PDF mirror instead, so newer revisions may differ.
- **Cookbook coding/performance guidelines on pdfcoffee.com:** HTTP 403.
- **OpenTitan `hw/dv/sv/dv_utils/README.md`:** the file is empty in the repo. The DV_CHECK and randomization macro conventions come from LR-DV instead.
- **Cummings SNUG event-region guidelines PDF** (sunburst-design.com): the direct link returned HTML, not the PDF.
- **AMIQ Verissimo rule catalogue:** not published; it is generated from the tool.
- **Google's internal SV style guide:** not public. Verible's "[Style: …]" citations point to it.
- **No separate published DV style guide** for Ibex, CVA6, PULP or Caliptra; they all defer to lowRISC. ChipVerify and Verification Guide are tutorials, not style guides.
