# Research A: UVM methodology guidelines

Raw research notes (web, 2026-10-15). Input to [../sv-uvm-rules-assessment.md](../sv-uvm-rules-assessment.md). IDs here are the researcher's own and are cited from the assessment.

I found 128 separate guidelines across 14 sources. Most can be checked statically over the slang AST (structure, call sites, method-local flow). Only about 20 are judgment calls or depend on the component tree built at run time. I read every source listed below as a full text, except where the inaccessible list says otherwise. No files were written; the tables follow inline.

**Check classes**
- **S** – syntactic: one construct, possibly with the called method or type resolved.
- **D** – declaration/structure: class members, method signatures, the base-class chain, ordering inside a class.
- **X** – cross-reference: across classes or files, or string-keyed.
- **F** – dataflow/control flow/call graph: ordering, guards, loops, "all paths".
- **N** – naming: a regex on identifiers or string literals.
- **H** – needs the runtime-built UVM component hierarchy, so it can only be approximated statically.
- **J** – judgment; not statically checkable.

**Source keys**
- **EUVM** – Doulos Easier UVM summary (v2016-06-24). Cited by section: Lex, Gen, Struct, Clk, Tx, Seq, Stim, Obj, Comp, DUT, TLM, Cfg, Fac, Test, Msg, Reg, Cov.
- **MGC-UVM n.n / MGC-SV n.n** – Mentor Verification Academy Cookbook appendix "UVM/Guidelines" and "SV/Guidelines" (PDF snapshot dated 2013-09-04).
- **SF15** – Sutherland & Fitzpatrick, "UVM Rapid Adoption", DVCon 2015.
- **AE11** – Erickson, "Are OVM & UVM Macros Evil?", DVCon 2011.
- **VL** – Verilab DVCon tutorial "Advanced UVM in the real world" (Litterick, Sprott, Bromley, Cooper).
- **VC14** – Cooper & Marriott (Verilab), "Demystifying the UVM Configuration Database", DVCon 2014.
- **MRL** – Litterick & Harnisch (Verilab), "Advanced UVM Register Modeling", DVCon 2014.
- **RE-GF / RE-ACM / RE-WO** – Edelman (Mentor/Siemens): "Go Figure", "Avoiding Configuration Madness", "Without Objection".
- **CS20** – Chris Spear, "UVM Configuration DB Guidelines" (Siemens blog, 2020).
- **CC-EVT** – Cummings & Salz, "SystemVerilog Event Regions, Race Avoidance & Guidelines", SNUG 2006.
- **CC-TERM** – Cummings, "OVM & UVM Techniques for Terminating Tests", DVCon 2011.
- **CC-1800** – Cummings, "IEEE 1800.2 UVM – Changes, Useful UVM Tricks & Techniques" (DVCon slides). These slides restate his transaction, messaging and analysis-port guidelines.
- **LR** – lowRISC `DVCodingStyle.md`.
- **UG12** – Accellera UVM 1.2 User's Guide.
- **CV** – ChipVerify ("UVM Introduction" and "Reusable verification components" pages).

## A. Packaging, files, preprocessor
| ID | Guideline | Sources | Chk | Static info needed |
|---|---|---|---|---|
| A1 | Define every class inside a package, not in a module or at file scope | EUVM/Struct; MGC-UVM 1.1; MGC-SV 4.4 | D | Scope that encloses the class declaration |
| A2 | No code in `$unit`; no wildcard import at compilation-unit scope | EUVM/Struct; MGC-SV 4.4 | S | Items at compilation-unit level |
| A3 | One class per file, file named `<class>.svh` (a sequence-library file is an allowed exception, VL) | MGC-UVM 1.2; MGC-SV 3.3; LR; VL | D | Map from class to source file name |
| A4 | `` `include `` a file in one place only (its package); everywhere else use `import` | EUVM/Struct; MGC-SV 3.5, 3.7 | X | Include graph across all compilation units |
| A5 | Include guards on include files. LR: global macro headers guarded, local macros unguarded and `` `undef ``'d | EUVM/Struct; LR | S | Preprocessor token stream |
| A6 | `` `include "uvm_macros.svh" `` and `import uvm_pkg::*` in every package/module that uses UVM | EUVM/Struct; SF15 | X | Where each UVM symbol resolves |
| A7 | Package names end in `_pkg` | EUVM/Lex; MGC-UVM 1.2 | N | — |
| A8 | `.sv` for compiled files, `.svh` for included files; lower-case file names | MGC-SV 3.1, 3.2 | N | File names plus include graph |
| A9 | Import the packages (especially the test package) whose classes are factory-registered | MGC-UVM 2.2 | X | Which packages the top module imports vs which hold `uvm_test` subclasses |
| A10 | Macro hygiene: prefix macro names, parenthesize arguments, wrap multi-statement bodies in `begin`/`end`, `` `undef `` local macros at end of file | LR | S | Macro definitions (preprocessor) |
| A11 | Package functions explicitly `static` or `automatic`; package parameters explicitly typed | LR | S | — |

## B. Naming
| ID | Guideline | Sources | Chk | Static info needed |
|---|---|---|---|---|
| B1 | lower_snake_case for variables and classes (no camelCase) | EUVM/Lex; MGC-SV 1.7 | N | — |
| B2 | UPPER_SNAKE for enum literals, constants and parameters. **Conflict:** LR wants enum values in UpperCamelCase | EUVM/Lex; LR | N | Kind of each declaration |
| B3 | `m_` prefix on class members. Scope differs: EUVM all members; MGC private/protected only; LR class-handle members only | EUVM/Lex; MGC-SV 2.2; LR | N+D | Member kind, visibility, type |
| B4 | Agent child instance names: `m_sequencer`/`m_driver`/`m_monitor` (EUVM) vs `driver`/`monitor` (LR); `_env`/`_agent` suffix on instances | EUVM/Lex; LR | N+D | Agent class identified via the `uvm_agent` base chain |
| B5 | Config naming: instance `m_config` (EUVM) or `cfg` (LR); class `<comp>_config` (EUVM) or `_cfg` | EUVM/Lex, Cfg; LR; MGC-SV 1.8 | N+X | Link from config class to the component that owns it |
| B6 | Suffixes `_port`, `_export`, `_vif`, `_t` (typedef); MGC also uses `_e`, `_h`, `_ap`, `_cfg` | EUVM/Lex; MGC-SV 1.8; LR | N+D | Declared type of each variable |
| B7 | Instance-name strings use only `[A-Za-z0-9_]` | EUVM/Lex | N | Literal arguments to `create`/`new` |
| B8 | The `create("x")` name string equals the variable it is assigned to; append the index for arrays | EUVM/Tx, Seq, Comp; MGC-UVM 2.3; LR | S | Assignment target vs first argument of `create` |
| B9 | Name constraints with a `_c` suffix | LR | N | — |
| B10 | DPI imports prefixed `c_dpi_`, exports `sv_dpi_` | LR | N | — |
| B11 | User plusargs never start with `uvm_`/`UVM_`; use a company prefix | MGC-UVM 11.1, 11.2 | S | String argument to `$test$plusargs`, `$value$plusargs`, `uvm_cmdline_processor` |
| B12 | End labels on class, function, task and package | MGC-SV 1.10 | S | — |
| B13 | Register-model member names in UPPERCASE (avoids clashes with base-class symbols) | UG12 | N+D | Classes derived from `uvm_reg*` |
| B14 | Don't call the local cast handle `rhs_`; use a distinct name such as `tr` | CC-1800 | N | Locals inside `do_*` methods |
| B15 | Label coverpoints and crosses; name bins explicitly | MGC-SV 4.12, 4.13 | S | — |

## C. Class structure and registration
| ID | Guideline | Sources | Chk | Static info needed |
|---|---|---|---|---|
| C1 | Register every class: `` `uvm_object_utils ``/`` `uvm_component_utils `` (`_param_` variants for parameterized classes). The macro's argument must equal the enclosing class, and the object/component flavour must match the base. Exceptions: TLM ports, covergroups, optionally register models (L5) | EUVM/Tx, Seq, Comp; MGC-UVM 2.1, 3.1; AE11 ("always use"); VL; LR; SF15 | D | Macro expansion in the class body, base chain, whether the class is parameterized |
| C2 | Registration macro placement. **Conflict:** first line of the class (EUVM) vs after member declarations (LR) | EUVM; LR | D | Order of class items |
| C3 | Class order: macro → ports/exports/vifs → members → constructor → methods | EUVM/Comp, Tx | D | Order of class items |
| C4 | Object constructor is `new(string name="")` with default `""` | EUVM/Tx, Seq; LR | D | Constructor signature + base chain |
| C5 | Component constructor is `new(string name, uvm_component parent)` with no defaults | EUVM/Comp; LR | D | Same |
| C6 | No constructor arguments beyond name/parent | MGC-UVM 1.4; LR | D | Same |
| C7 | `super.new(...)` is the first statement; the constructor is otherwise empty (covergroup `new` allowed) | MGC-UVM 1.3; LR; EUVM | S/D | Constructor body |
| C8 | Don't write `virtual` on overrides of UVM virtual methods | EUVM/Lex | D | Find the overridden base method |
| C9 | Declare methods `extern` | MGC-SV 2.3 | S | — |
| C10 | Never implement `create()` by hand | CC-1800 | D | Method names in the class |
| C11 | Don't use undocumented UVM internals (e.g. `m_*` members, `uvm_agent::is_active`) | EUVM/Gen; SF15 | X | Deny-list of UVM API names |
| C12 | Don't use deprecated UVM API (see N-rows) | EUVM/Gen; MGC-UVM 6.1; VL; LR | X | Deprecation list (1800.2 / `UVM_NO_DEPRECATED`) |
| C13 | Members meant to be randomized must not be `local`/`protected` | MGC-SV 2.2 | D | `rand` + visibility |

## D. Factory
| ID | Guideline | Sources | Chk | Static info needed |
|---|---|---|---|---|
| D1 | Create components, objects, items and sequences with `T::type_id::create`, never `new()` | EUVM/Fac; MGC-UVM 2.1; VL; LR; SF15; CV | S | Resolved type of `new` target is a `uvm_object` subclass, minus exceptions |
| D2 | TLM ports/exports/FIFOs and covergroups use `new()`, not the factory | SF15; VL; LR | S | Type of the `new` target |
| D3 | Create components in `build_phase` (not in the constructor or `run_phase`) | EUVM/Comp; VL; LR; CV | F | Method that encloses each `create` call |
| D4 | Second argument of a component `create` is `this` | EUVM/Comp; LR; SF15 | S | — |
| D5 | Use type-based overrides (`orig::type_id::set_type_override(new::get_type())`), not name-based | EUVM/Fac; VL | S | Which override API is called |
| D6 | Override type must extend the original type | VL | X | Subtype check on the two type arguments |
| D7 | Overrides must be in place before the instance is created (in the test, before `super.build_phase`/`create`) | VL; LR; SF15 | F/H | Statement order within `build_phase`; across levels needs H |
| D8 | Get the factory via `uvm_factory::get()` | EUVM/Fac | S | Use of the global `factory` |
| D9 | `type_id::create` names an explicit class, not a type parameter | LR | S/D | Whether the scope prefix is a type parameter |
| D10 | Create the original type, not the override type | VL | J | — |

## E. Transactions
| ID | Guideline | Sources | Chk | Static info needed |
|---|---|---|---|---|
| E1 | Transactions extend `uvm_sequence_item` (not `uvm_component`) | EUVM/Tx; SF15; LR; CV | D | Base chain |
| E2 | Don't use `` `uvm_field_* `` macros. **Conflict:** LR uses them on items (never on components); CV recommends them; SF15 authors disagree | EUVM/Tx; MGC-UVM 3.3; AE11; CC-1800; LR; CV; SF15 | S | Macro calls (+ enclosing class kind for the LR variant) |
| E3 | Implement `convert2string`, `do_copy`, `do_compare`, `do_print`, `do_record` (and `do_pack`/`do_unpack` where needed) | EUVM/Tx; SF15; MGC-UVM 3.3; CC-1800 | D | Method set on the item class |
| E4 | Override `do_*`, never `copy`/`compare`/`print`/`pack` themselves | CC-1800 | D | Names of overridden methods |
| E5 | Inside `do_print`/`do_compare`/etc. don't use the policy argument (printer/comparer…) | EUVM/Tx | F | Uses of the parameter |
| E6 | In `do_copy`/`do_compare`: `$cast` rhs and check the result, call `super.do_*`, use `===` for 4-state fields | AE11; MGC-SV 4.1 | F | Method body |
| E7 | In `do_pack`/`do_unpack`: call super first; unpack in the same order as pack | AE11 | F/X | Compare statement order across two methods |
| E8 | `rand` on stimulus fields; response/output fields not `rand` | EUVM/Tx; MGC-UVM 4.4; CC-1800 | D/J | `rand` qualifier (intent is judgment) |
| E9 | Item holds only transaction data and is legal when randomized | VL; LR | J | — |
| E10 | Prefer `convert2string()`; avoid `print()`/`sprint()` | SF15; CC-1800; LR | S | Call sites |
| E11 | Driver and monitor share one item class; keep item classes few | EUVM/Tx | X/J | — |
| E12 | Monitor creates a new item (`create` or `clone`) for each `ap.write` | SF15 | F | Allocation reaches the write inside the loop |
| E13 | Never modify a received broadcast transaction; copy first (MCOW) | MGC-UVM 10.1; CC-1800 | F | Writes through the `write()` argument |
| E14 | Driver must not randomize items it receives | LR | F | `randomize()` on the item obtained from `get_next_item` |

## F. Sequences
| ID | Guideline | Sources | Chk | Static info needed |
|---|---|---|---|---|
| F1 | Extend `uvm_sequence #(REQ)` with the item type | EUVM/Seq | D | Base specialization |
| F2 | Use `create` / `start_item` / `randomize` / `finish_item`; no `` `uvm_do* `` macros. **Conflict:** VL says uvm_do randomizes late, which is useful for reactive sequences | EUVM/Seq; MGC-UVM 4.2; AE11; LR; CC-1800 (most variants deprecated in 1800.2) | S | Macro calls |
| F3 | Always check the result of `randomize()`, with `if`, not `assert` | EUVM/Seq; MGC-SV 4.2, 4.3; LR | S/F | Discarded or `assert`-wrapped `randomize` calls |
| F4 | Randomize a sequence object before `start` | EUVM/Seq; LR | F | `x.randomize` precedes `x.start` |
| F5 | Start sequences procedurally with `seq.start(sqr)` | EUVM/Seq; MGC-UVM 4.1; SF15 | S | — |
| F6 | No `default_sequence`, no automatic start, no sequence-library macros, no `count` | SF15; AE11; VL; CC-1800 | S | String `"default_sequence"` in `config_db` `set`; macro names |
| F7 | Use `pre_start`/`post_start`, not `pre_body`/`post_body` | EUVM/Seq; MGC-UVM 4.3; LR | D | Overridden methods |
| F8 | `body()` holds only functional behaviour; housekeeping goes in `pre_start`/`post_start` | EUVM/Seq; LR | F/J | Objection calls inside `body` |
| F9 | Only override `pre_do`/`mid_do`/`post_do` to adapt an existing sequence | EUVM/Seq | J | — |
| F10 | Use `` `uvm_declare_p_sequencer `` instead of a hand-written `p_sequencer` + `$cast` | EUVM/Seq | S | — |
| F11 | Sequences consume no explicit time; LR allows clock-cycle waits | MGC-UVM 4.5; LR | S/F | `#` delays in `uvm_sequence` subclasses and their callees |
| F12 | Virtual sequences: started on null (EUVM) or null/valid virtual sequencer (LR); a virtual sequencer is unnecessary (SF15) | EUVM/Stim; LR; SF15 | S | Argument to `start` |
| F13 | With a virtual sequencer, check sub-sequencer handles for null before use | MGC-UVM 4.6 | F | Null guard before dereference |
| F14 | Items come only from sequences, not components or ad-hoc classes | EUVM/Seq | X | `start_item`/`finish_item` call sites outside sequence classes |
| F15 | Sequences are not phase-aware | EUVM/Stim | F/J | Phase references inside sequences |
| F16 | Sequence config: get from `config_db` at the start of the sequence (EUVM) vs avoid pulling from `config_db` (VL). **Conflict** | EUVM/Cfg; VL | F | — |
| F17 | Use the built-in `req`/`rsp` handles | EUVM/Seq | N/J | — |

## G. Phasing, objections, end of test
| ID | Guideline | Sources | Chk | Static info needed |
|---|---|---|---|---|
| G1 | Don't call `super.build_phase` when directly extending a UVM base class (it triggers `apply_config_settings`). **Conflict:** VL and VC14 say call it, because auto-config needs it | EUVM/Comp; SF15; CS20; VL; VC14 | D+F | Base chain + whether a `super` call is present |
| G2 | When extending a user component, call `super.<phase>_phase` in every overridden phase | EUVM/Comp; LR; VL | D+F | Same |
| G3 | Drivers/monitors/scoreboards implement only `run_phase`, no run-time sub-phases | EUVM/Stim; MGC-UVM 5.1; SF15 | D | Phase methods by component kind |
| G4 | Avoid `reset`/`configure`/`main`/`shutdown` phases (EUVM allows them in test/env but bans overriding `pre_`/`post_` versions) | MGC-UVM 5.2; SF15; RE-WO; EUVM | D | Overridden methods |
| G5 | No phase jumping or phase domains | MGC-UVM 5.3; SF15 | S | `jump`/domain API calls |
| G6 | No user-defined phases (EUVM allows them) | MGC-UVM 5.4; RE-WO | D | Classes extending `uvm_*_phase` |
| G7 | Port connections and vif assignments in `connect_phase` | EUVM/TLM; CV | F | Method that encloses `.connect()` |
| G8 | `config_db` get/set in `build_phase` | EUVM/Cfg | F | Enclosing method of the call |
| G9 | End the test with objections; extend it with `phase_ready_to_end`; no `$finish`, nothing after `run_test()` | MGC-UVM 8.1; SF15; CC-TERM; LR | S/F | `$finish` in classes; statements after `run_test` |
| G10 | Raise/drop only in tests (MGC); never per transaction, in driver/monitor loops or `start_item`. LR allows a monitor watchdog in `phase_ready_to_end` | MGC-UVM 8.2; EUVM/Obj; RE-WO; SF15; LR | F | Raise/drop inside loops; enclosing class kind |
| G11 | Every raise has a matching drop on all paths (implied by all sources' patterns, not stated as a rule) | (implicit) | F | Path analysis per method |
| G12 | Pass a description string to `raise_objection`/`drop_objection` | EUVM/Obj; LR | S | Number of arguments |
| G13 | In a sequence, guard raise/drop with `starting_phase != null`; override `do_kill` to drop the objection | EUVM/Obj | F/D | Null guard; `do_kill` present |
| G14 | Call `set_propagate_mode(0)` on objections | EUVM/Obj | X | — |
| G15 | Raise the objection before the first time-consuming statement of the phase | SF15 | F | Statement order |
| G16 | Don't use automatic per-sequence objections | VL | S | `set_automatic_phase_objection` |
| G17 | Set timeouts before `run_test` (CC) / in the base test via `set_timeout` (LR); not in a run task | CC-TERM; LR | F | Where the timeout call sits |
| G18 | Call `run_test()` with no argument (use `+UVM_TESTNAME`) | LR; SF15 | S | — |
| G19 | Bound every `wait` and non-forever loop with a watchdog; use an isolation fork + `disable fork`, not disable-by-label | LR | S/F | Fork/disable shapes |

## H. Components, agents, TLM
| ID | Guideline | Sources | Chk | Static info needed |
|---|---|---|---|---|
| H1 | Extend the matching `uvm_*` subclass to show intent (SF15: an agent may extend `uvm_component`) | EUVM/Comp; SF15 | D | Base chain |
| H2 | One agent per DUT interface | EUVM/Struct; LR | H/J | — |
| H3 | An agent contains only sequencer, driver, monitor (+ config) | EUVM/Struct; LR | D | Types of component members |
| H4 | Build/connect sequencer and driver only when `get_is_active()` (EUVM) or `cfg.is_active` (LR, SF15) says active | EUVM/Cfg; LR; SF15 | F | `create` calls dominated by an is-active test |
| H5 | Agent/driver checks its vif was obtained; `uvm_fatal` if `get` fails | EUVM/DUT; VC14; LR | F | Result check after `get` |
| H6 | A monitor never assigns interface signals | EUVM/Clk; SF15 | F | Writes through vif inside `uvm_monitor` subclasses |
| H7 | A monitor doesn't rely on driver state | UG12 | X | Monitor referencing driver |
| H8 | A driver has one sequencer and no analysis ports | LR | D | Member types |
| H9 | Driver pulls items. **Conflict:** `try_next_item` (EUVM) vs `get_next_item` (SF15) | EUVM/Clk; SF15 | S | — |
| H10 | Checking/coverage in subscribers/scoreboards outside the agent, fed from the monitor `ap` (SF15 prefers a coverage collector inside the agent) | EUVM/Struct; SF15 | D/H | — |
| H11 | Components talk via ports/exports, not handles; no `uvm_top.find` in reusable components | EUVM/TLM; CV | S/X | Calls to `find`; cross-component handle use |
| H12 | Connect port→export directly, with no FIFO in between, for peer-to-peer | EUVM/TLM | X | — |
| H13 | Use `` `uvm_analysis_imp_decl `` for multiple analysis inputs | EUVM/Cov; AE11; CC-1800 | D | — |
| H14 | Scoreboards buffer in `uvm_tlm_analysis_fifo`; LR: check FIFOs empty in `check_phase` | LR; CC-1800 | D/F | — |
| H15 | Only drivers/monitors contain timing; other components are untimed; delays live in the transaction | EUVM/Clk, Split | F | `#`/`@` by component kind |
| H16 | Don't use UVM callbacks (use OOP/factory) | MGC-UVM 9.1; SF15 | S | `` `uvm_register_cb ``, `uvm_callbacks#` |
| H17 | Don't use `uvm_comparator`/`uvm_in_order_comparator`/`uvm_algorithmic_comparator` (removed from 1800.2) | CC-1800 | S | — |

## I. Configuration
| ID | Guideline | Sources | Chk | Static info needed |
|---|---|---|---|---|
| I1 | Use `uvm_config_db`, not `uvm_resource_db` or `set/get_config_*` | EUVM/Cfg; MGC-UVM 6.1; LR | S | — |
| I2 | Pass config objects, not individual ints/strings | MGC-UVM 6.1, 6.2; LR; RE-GF; RE-ACM; CS20; VL; EUVM; CV | S | `config_db #(T)` where T is not a class or virtual interface |
| I3 | Config classes extend `uvm_object` | EUVM/Cfg | D | — |
| I4 | Always check the bit returned by `get`; fatal (LR, VC14) or a sensible default (EUVM) | EUVM/Cfg; LR; VC14 | S/F | Discarded return value |
| I5 | No wildcards in the field name (LR, RE-GF); wildcards only at the end of the scope (CS20); avoid `"*"` global scope (VC14); no absolute paths, use the shortest unique path (EUVM) | LR; RE-GF; CS20; VC14; EUVM | N | String literals of `set` |
| I6 | Each `set` has a matching `get` with the same key/type. The rule is implied, not stated; VC14 shows a debug case caused by a misspelled field name | (implicit, VC14) | X | String-keyed matching of set/get pairs + type parameter |
| I7 | Type parameter of `set` equals that of `get` (`uvm_object` vs concrete class breaks); use the `uvm_config_object` typedef | VL | X | — |
| I8 | Field name `"config"` for config objects | EUVM/Cfg | S | — |
| I9 | Parent/ancestor sets a child's config; a component gets only its own; configs form a tree (top config holds sub-configs) | EUVM/Cfg; SF15; RE-ACM; VL; LR | X/H | — |
| I10 | Vif: `set` from the top module (`null, "uvm_test_top"`), stored in config objects, copied down in env `build_phase` | MGC-UVM 6.4; EUVM/DUT; SF15; CV | X | — |
| I11 | Don't use `config_db` for frequent communication; no `get` in monitor/scoreboard run loops | MGC-UVM 6.3; RE-GF | F | `get` inside loops or run-phase tasks |
| I12 | Register config classes with the factory? **Conflict:** EUVM says don't; VL and SF15 create configs via the factory | EUVM/Cfg; VL; SF15 | D | — |
| I13 | Don't rely on field-macro auto-configuration | RE-GF; AE11 | S | — |
| I14 | Don't `set` a null handle (create the object first) | VL | F | Handle definitely assigned before `set` |
| I15 | Parent assigns child variables directly only in `build_phase`, after the child is created | EUVM/Cfg | F | — |

## J. Messaging
| ID | Guideline | Sources | Chk | Static info needed |
|---|---|---|---|---|
| J1 | Use `` `uvm_info ``/warning/error/fatal macros; no `$display` or `uvm_report_*` calls | EUVM/Msg; MGC-UVM 3.2; AE11; LR; CC-1800; CV | S | — |
| J2 | Message ID is a static string or `get_type_name()` (LR: `gfn`/`gtn`) | EUVM/Msg; LR | S | First macro argument |
| J3 | Verbosity. **Conflict:** default high (EUVM); `UVM_MEDIUM` normally, `UVM_LOW` almost never (CC); never `UVM_NONE`/`UVM_FULL` (LR) | EUVM; CC-1800; LR | S | Verbosity literal |
| J4 | Don't use `` `uvm_warning `` (LR) / use it sparingly (CC) | LR; CC-1800 | S | — |
| J5 | Avoid `` `uvm_info_begin ``/`_end` and `uvm_coreservice_t` | SF15 | S | — |

## K. Coverage
| ID | Guideline | Sources | Chk | Static info needed |
|---|---|---|---|---|
| K1 | Covergroups in components (subscribers/scoreboards), not in items or sequences; MGC: in wrapper classes | EUVM/Cov; MGC-UVM 7.1; MGC-SV 4.9 | D | Enclosing class kind |
| K2 | Construct the covergroup in the constructor | EUVM/Cov; MGC-UVM 1.3 | F | — |
| K3 | Sample with `sample()`; no clocking event on the covergroup | EUVM/Cov; MGC-SV 4.11 | S | — |
| K4 | Conditional sampling (`iff`), plus a `coverage_enable` knob in the config | EUVM/Cov; MGC-SV 4.10; CV | S/D | — |
| K5 | Don't set `option.weight`/`option.goal`; consider `at_least` | EUVM/Cov | S | — |
| K6 | `ignore_bins`, not `illegal_bins` (EUVM); `illegal_bins` never as the only checker (LR) | EUVM/Cov; LR | S | — |
| K7 | Keep bin counts small (watch auto-bins and crosses) | MGC-SV 4.14 | S/J | Width of auto-binned expressions |
| K8 | Sample DUT outputs, not stimulus | EUVM/Cov | J | — |

## L. Register layer
| ID | Guideline | Sources | Chk | Static info needed |
|---|---|---|---|---|
| L1 | Don't edit a generated register model | EUVM/Reg | J | — |
| L2 | Env has a `regmodel` variable; build only if null, via factory, then `build()` and `lock_model()` | EUVM/Reg; UG12 | D/F | — |
| L3 | Per agent: adapter + explicit predictor; predictor `.map` set to the top-level map | EUVM/Reg | X/H | — |
| L4 | Register sequences extend `uvm_sequence`, have `regmodel`, set before `start` | EUVM/Reg | D/F | — |
| L5 | Large models: optionally no factory for registers. Contrasts with D1 | VL; MRL | — | — |
| L6 | Memory properties not `rand`; `post_randomize` overrides call super | UG12 | D/F | — |
| L7 | `uvm_reg::set()`/`update()` are not atomic; beware parallel threads | LR | J | — |

## M. SystemVerilog language and testbench timing
| ID | Guideline | Sources | Chk | Static info needed |
|---|---|---|---|---|
| M1 | Clocks/resets generated in modules: not in classes (LR: not in `uvm_object`), not in programs | EUVM/Clk; CC-EVT; LR | D/F | Clock-toggle loops inside classes/programs |
| M2 | Don't use `program`. **Conflict:** CC-EVT (2006) encouraged programs for testbench code | EUVM/Clk; MGC-SV 4.7; LR | S | — |
| M3 | Clocking blocks in interfaces; modports enforce their use through vifs. SF15 caveat: bad for emulation | EUVM/Clk; LR; CC-EVT | D/X | Interface contents; vif modport type |
| M4 | `#1step` only inside clocking blocks; never non-unit steps | CC-EVT | S | — |
| M5 | No `#0` procedural delays | MGC-SV 4.6; CC-EVT | S | — |
| M6 | Nonblocking for sequential logic, blocking for combinational; no variable assigned from more than one `always`. Apply stimulus on the inactive edge or through a clocking block | CC-EVT | S/F | — |
| M7 | Check the result of `$cast` | MGC-SV 4.1; AE11 | S | — |
| M8 | No side-effecting calls inside `assert()` | MGC-SV 4.3; LR | S | — |
| M9 | No `[*]` associative arrays | MGC-SV 4.5; LR | S | — |
| M10 | Avoid `final` and `checker` | MGC-SV 4.7 | S | — |
| M11 | Don't rely on the order of static variable initialization | MGC-SV 4.8 | F/X | Static initializers that reference other statics |
| M12 | Lexical: one statement per line, `begin`/`end` on conditionals, parenthesized conditions, spaces not tabs, ~80 columns | MGC-SV 1.1–1.6; EUVM/Lex | S | — |
| M13 | Forbidden: `$psprintf`, `$random`, `$dist_*`, `$srandom` | LR | S | — |
| M14 | No `void'()` on calls with a useful result (except system functions, `predict`, FIFO/queue methods) | LR | S | — |
| M15 | Constraints: no loops/calculations inside them; prefer `inside` and bitmasks | LR | S | — |
| M16 | DUT hierarchical references confined to one module/interface reached via vif; backdoor uses ports, not internal nets | EUVM/Cov; LR | S | Hierarchical names inside classes |
| M17 | Emulation: separate HDL/HVL top modules, untimed HVL side, split driver/monitor | EUVM/Split; SF15; MGC | X/H | — |

## T. Tests (mostly judgment)
| ID | Guideline | Sources | Chk |
|---|---|---|---|
| T1 | **Conflict:** the test only sets config and overrides, with no stimulus (EUVM), vs the test starts sequences in `run_phase` (SF15, MGC-UVM 4.1) | EUVM/Test; SF15; MGC | F |
| T2 | Env produces default stimulus so an empty test runs; use base-test classes; agents live in env, not test | EUVM/Test; SF15; CV | J/D |

## Contradictions a rule pack will need options for
- **Field macros:** E2.
- **`super.build_phase` in direct UVM subclasses:** G1.
- **Run-time phases and user-defined phases:** G4, G6.
- **Objections in sequences:** F8/G13 vs VL "roughly, don't".
- **Driver item pull:** `try_next_item` vs `get_next_item` (H9).
- **Registration macro placement:** C2.
- **Config classes and the factory:** I12.
- **Virtual sequencer:** F12.
- **Verbosity rules:** J3.
- **Enum value case:** B2.
- **`program` blocks:** M2.
- **What the test does:** T1.

ChipVerify also recommends field macros, which no methodology paper does.

## Sources read (full text)
- https://www.doulos.com/knowhow/systemverilog/uvm/easier-uvm/easier-uvm-coding-guidelines/summary-of-the-easier-uvm-coding-guidelines/ (full summary, all sections)
- Mentor Cookbook PDF snapshot, including the appendices "SV/Guidelines" and "UVM/Guidelines": https://iccircle.com/static/upload/img20240207093017.pdf
- https://sutherland-hdl.com/papers/2015-DVCon_UVM-rapid-adoption_paper.pdf
- https://dvcon-proceedings.org/wp-content/uploads/are-ovm-uvm-macros-evil-a-cost-benefit-analysis.pdf
- https://cdn.prod.website-files.com/63f4bb21bd5303fe472ad00e/64ac5ce45f77f569ae2557af_verilab_dvcon_tutorial_a%20(1).pdf
- https://dvcon-proceedings.org/wp-content/uploads/demystifying-the-uvm-configuration-database.pdf
- https://dvcon-proceedings.org/wp-content/uploads/advanced-uvm-register-modeling.pdf
- https://dvcon-proceedings.org/wp-content/uploads/go-figure-uvm-configure-the-good-the-bad-the-debug-paper.pdf
- https://dvcon-proceedings.org/wp-content/uploads/1118-Avoiding-Configuration-Madness-The-Easy-Way.pdf
- https://dvcon-proceedings.org/wp-content/uploads/1116.pdf ("Without Objection")
- https://blogs.sw.siemens.com/verificationhorizons/2020/05/07/uvm-configuration-db-guidelines/
- https://rfsoc.mit.edu/6S965/_static/F25/lectures/CummingsSNUG2006Boston_SystemVerilog_Events.pdf
- https://dvcon-proceedings.org/wp-content/uploads/ovm-uvm-techniques-for-terminating-tests.pdf
- https://dvcon-proceedings.org/wp-content/uploads/ieee-1800-2-uvm-changes-useful-uvm-tricks-techniques.pdf
- https://raw.githubusercontent.com/lowRISC/style-guides/master/DVCodingStyle.md
- https://accellera.org/images/downloads/standards/uvm/uvm_users_guide_1.2.pdf (searched for recommendations, not read cover to cover)
- https://chipverify.com/uvm/uvm-introduction and https://chipverify.com/uvm/reusable-verification-components

## Could not access or not covered
- **Verification Academy online Cookbook pages** ("UVM Performance Guidelines", "SystemVerilog Performance Guidelines") are members-only. I used the 2013 PDF snapshot instead, so newer Cookbook guidance may be missing.
- **Cummings, "UVM Transactions – Definitions, Methods and Usage" (SNUG 2014):** the sunburst-design.com link redirects to a Paradigm Works library page that does not serve the PDF; the mirrors were behind Cloudflare or truncated. Its guidelines are taken from his CC-1800 slides.
- **Other Cummings papers:** the "UVM Messages" paper (SNUG 2014) and "OVM/UVM Scoreboards" (SNUG 2013, on Scribd/Academia) were not fetched. The analysis-port/copy paper is likewise covered only through the CC-1800 slides.
- **Cummings, "Nonblocking Assignments in Verilog Synthesis" (SNUG 2000):** the "8 guidelines" figure in CC-EVT is an image. Its text quotes only guidelines #1–4, #6, #7 and #8, so #5 is not in the tables.
- **Doulos "Detailed Explanation" page:** not read; only the one-line summary versions are used.
- **"Parameters and OVM":** found, but it is by Bryan Ramirez of Xilinx, not Verilab, and I did not read it.
- **Not found at all:** a separate Verilab "config_db use and abuse" paper, any public Cadence UVM coding-guideline document, and Synopsys VMM→UVM guidance. The 1800.2 deprecations come only from CC-1800 and LR, not from the IEEE 1800.2 standard text.
