# A queryable database of constructed models

Status: proposal, 2026-10-15. Nothing here is decided.
Follows [`sv-uvm-elaboration-checks.md`](sv-uvm-elaboration-checks.md), which builds an
elaborated UVM model ("facet") for lint, and pssparser `docs/design/rules-over-models.md`,
whose model contract (generated schema, node handles, model identity) this reuses.

**The idea (user, 2026-10-15):** we construct the model for checking, but much of it is very
useful as a queryable database. Consider caching the constructed model in a database for fast
queries.

## 0. Answer

> **Yes.** The expensive part of everything in the elaboration note is *constructing* the model:
> compiling, resolving and interpreting run-0 per test. Rules are cheap by comparison. Once
> it is built, the model answers questions nobody can answer quickly today:
> - "what type is `env.agent[1].drv` in test `dma_err`, and why";
> - "who sets `vif` for this agent";
> - "which tests instantiate this class";
> - "what RTL does this agent drive".
>
> **Proposal.** Persist each constructed model as a **facts database**. The schema is generated
> from the same model schema the rule engine uses, so the database, the rules and the docs
> share one definition.
>
> Its main users:
> 1. **people**, through the CLI, the editor and the docs;
> 2. **agents**, through MCP, so an agent asks for facts instead of reading 30 files;
> 3. **lint itself**, as a cache: rules skip reconstruction when the inputs are unchanged.
>
> **Recommended technology: SQLite**, with a **generated schema** of tables plus **views** as
> the stable query API.
> - It is embedded, a single file, in the Python stdlib, available from C++, and has a WASM
>   build.
> - Recursive CTEs cover hierarchy queries, WAL mode allows concurrent reads while the editor
>   writes, and FTS5 handles name search.
> - **SQL** is the open query surface because people and LLMs already write it. **CEL**, the
>   rule language, stays available as a second query surface over the same model (§4).
>
> **The parts that need design, not just a schema:**
> - **stable identity**, so rows survive a rebuild and two builds can be diffed;
> - **freshness**: what a cached fact depends on, so it is invalidated exactly when it should be;
> - **keeping the unknowns**. The `may`/`must` modality, multiplicity and havoc points must be
>   columns, not dropped, or a query gives false confidence.
>
> **Precedents.** This is how code intelligence works at scale:
> - CodeQL: extract to a database, then query;
> - Glean (Meta): a schema'd fact store with derived facts, incrementally updated;
> - Kythe and SCIP/LSIF: precomputed cross-reference indexes served without the compiler.
>
> In EDA, Verdi's KDB is the standing example of persisting a compiled and elaborated design for
> fast debug queries. Nothing comparable exists for the *testbench's run-time structure*.

## 1. What goes in

The database holds layers, each keyed to what it depends on:

| Layer | Content | Built from | Depends on |
|---|---|---|---|
| **Files** | path, content hash, language, include edges, macro definitions | preprocessor | the file and its includes |
| **Declarations** | packages, classes (with base chains), members, methods, modules, interfaces; qualified names; source spans | slang semantic model; pssparser AST | the compilation unit |
| **References** | use → declaration edges (go-to-definition and find-references, persisted) | slang / pssparser resolution | the compilation |
| **Static elaboration** | RTL instance tree, parameter values, port connections | slang elaboration | the compilation + top selection |
| **UVM facet, level 1** | per class: creation sites, `connect` edges, config_db accesses, overrides, vif bindings | sphinx-systemverilog's extraction | the class's file + resolved types |
| **UVM facet, level 2** | per test root: instance tree (paths, final types), applied overrides, config set↔get matches, TLM graph, sequence starts, objection sites, register maps; havoc points | the run-0 interpreter | the root + every file the interpretation touched |
| **Cross-model links** | vif binding → RTL interface instance; HDL path → RTL symbol; register entry → SystemRDL node | the facet + other models | both sides |
| **Findings** (optional) | lint findings with rule ID, root, location | the rule engine | the facts + the rule set's hash |

PSS fits the same shape: declarations, references, and the linked component tree. A project
with both PSS and SV gets one database with model identity on every row. That is
rules-over-models §6.5's multi-model run, made concrete as data.

## 2. What it is for

| Consumer | Example questions | Today |
|---|---|---|
| **Engineer, CLI** | `svq instance env.agent[1].drv --test dma_err` gives the type after overrides, the create site, and the override that applied; `svq who-sets vif --at uvm_test_top.env.agt`; `svq tests-using axi_err_driver` | a simulator run with `print_topology` and `+UVM_CONFIG_DB_TRACE`, one test at a time |
| **Editor** | from a `config_db::get`, jump to the `set`(s) that satisfy it, per test; hover a `create` to see the final type in each test; "find instances" for a class | not available, except DVT's run-0 views for one test |
| **Docs** (sphinx-systemverilog) | trees, schematics, config tables and bridges read from the database instead of being rewalked on every build | recomputed every build; the slang `Compilation` cannot be pickled, so nothing persists |
| **Agents** (MCP) | "which agent drives `u_dut.axi`", "what config fields does `axi_agent` read", "show the sequences started on `env.vseqr`", returned as small structured answers | read files, grep, guess; `uvm-tb-proto` hand-builds a `FACTS.md` for exactly this |
| **Review / CI** | **model diff** between commits: "this change adds an agent in 12 tests, changes the override of `drv` in `dma_err`, and leaves `set('timeout')` unconsumed" | not available |
| **Lint** | the cache: unchanged roots are not reinterpreted; whole-model rules read persisted facts | everything rebuilt each run |

Two of these deserve emphasis:

- **Agents.** The FACTS.md in `uvm-tb-proto` (the shared preamble the generator emits for the
  LLM) is a hand-assembled projection of exactly this model. Generated from the database, it
  is complete and always current. An MCP `query` tool lets the agent ask instead of being
  told everything up front. This joins pssparser `docs/design/llm-guidance-from-rules.md`'s
  loop and the MCP surface in rules-over-models §5.3.
- **Model diff.** A testbench's structure changes silently: a base-class edit reshapes 40
  tests. Diffing two databases gives a review artifact no tool produces today. It falls out
  of stable identity (§3.1) at almost no extra cost.

## 3. Design

### 3.1 Identity

Every row needs an ID that is **the same in the next build** when the thing is the same, so the
database can be updated in place and two builds can be diffed. IDs come from semantic keys,
never from pointers or row order:

| Entity | Key |
|---|---|
| Declaration | model + qualified name (+ specialisation parameters for a class specialisation) |
| Reference | file + span of the use (+ content hash of the file, to detect staleness) |
| Creation site, config access, override | the enclosing declaration's key + its ordinal among such sites in that body. Not line numbers, which move on every edit above |
| UVM instance (level 2) | root (test + run-config hash) + instance path |
| Config match, TLM edge | the pair of endpoint keys |

A content-addressed `id = hash(key)` gives fixed-width integer keys and makes joins cheap.

### 3.2 Freshness and invalidation

Each fact records the inputs it was derived from. The database answers "is this still true?"
without rebuilding:
- **Files and declarations:** content hashes, plus the compilation context (file list,
  defines, include paths, tool and schema versions), hashed once per build.
- **Level-2 roots:** the interpreter records **every declaration it executed or read** while
  interpreting a root (its read set). Editing `axi_agent.svh` invalidates only the roots whose
  read set contains `axi_agent`, typically the tests that use that agent, not all 200.
- **Cross-model links and findings:** invalidated when either side's inputs change.

This is the Salsa/Glean model of incremental derived facts, at coarse grain. For a CLI run, the
database is a cache: hit or rebuild. For the editor it is updated incrementally on save. slang
recompiles the whole design anyway (real UVM compiles in about 0.1 s), so the cost worth
avoiding is interpreting many roots, and that is what the read sets save.

### 3.3 Keeping the unknowns

The elaboration note's abstraction is only honest if the database keeps it:
- `certainty` (`must`/`may`), `multiplicity` (`1`, `n`, `*`) and the `ok`/`opaque`/`unresolved`
  state are **columns** on every facet row.
- Havoc points are a table, and every root has a `complete` flag.
- The query views expose them by default. `who-sets` returns "two sets match; one is `may` (under
  `if (cfg.has_scoreboard)`)", not just two rows.
- A view whose answer depends on completeness ("orphan sets") includes it in the result.

A database that silently drops `may` rows would reintroduce the exact false confidence
sphinx-systemverilog's three node states were built to prevent.

### 3.4 Schema: generated, with views as the API

- **Tables are generated** from the model schema (rules-over-models §3.1): one table per kind,
  scalar fields as columns, node-valued fields as foreign keys, list fields as link tables.
  The rule engine, the database and the docs then cannot drift apart.
- **The base tables are internal.** They follow the schema and change with it.
- **The views are the public API.** `uvm_instance_v`, `config_match_v`, `who_sets_v`, `tlm_edge_v`
  and `class_usage_v` are hand-written, documented and versioned. A query in someone's script,
  or an agent's tool call, depends only on views.
- **Schema version** is stored in the file. A tool opening a newer major version refuses, as
  the rule loader does.

### 3.5 Storage choice

| Option | For | Against |
|---|---|---|
| **SQLite** (recommended) | embedded single file; stdlib Python, C API, official WASM build; recursive CTEs for hierarchy; WAL for editor-writes/CLI-reads; FTS5 for names; universally known to people and LLMs | row store, so large aggregations are slower (not a problem at testbench scale) |
| DuckDB | columnar, fast analytics, WASM, Python | weaker at many small incremental updates, which is what an editor does; a less settled file format |
| Datalog store (Soufflé, CodeQL-style) | recursive queries are natural | a query language few users know; lint-rule-expressions §2 already declined Datalog for rules |
| Graph database | path queries are natural | an extra service or dependency for what recursive CTEs already do |
| JSON / pickle files | trivial | no queries, no incremental update; fine only as the dump format (the E0b oracle) |

**Size.** Facts are small: a large testbench is tens of thousands of declarations and a few
thousand instances per root. Even 200 roots fit comfortably in a file of tens of MB.
Memoising shared subtrees (most tests share the env) keeps it smaller.

### 3.6 Two query surfaces, one model

- **SQL over the database**, for people, scripts, dashboards and agents. This is the open
  surface.
- **CEL over model handles**, the rule language as an ad-hoc query:
  `svcheck query 'UvmInstance' --where 'node.type.name.endsWith("_err_driver")'`. It works
  without a database (in WASM, on a fresh checkout) and uses the same functions as rules, so a
  query that proves useful can become a rule by adding an ID and a message.

The engine's node handles can be backed by either the live model or database rows. A rule never
knows which one it got, just as it doesn't know which facet producer ran.

### 3.7 Distribution

The database is a build artifact, like a CodeQL database or a remote build cache:
- **CI builds it per commit** and publishes it. A developer, an editor or an agent downloads
  the database for `HEAD` instead of building it.
- **Library-carried facts.** `uvm_pkg` and a VIP's level-1 facts are identical for every user.
  A VIP release could ship its facts database, as it could ship its rules, the same way
  precompiled headers ship. That is optional and later; it needs schema-version discipline
  first.

## 4. Order of work (proposed)

| Step | What | Depends on |
|---|---|---|
| **D0** | Generate SQLite DDL + views from the facet schema; write sphinx-systemverilog's level-1 model into it. sphinx-systemverilog persists across builds through it, fixing the pickling limit | elaboration note E0 |
| **D1** | `svq` CLI over the views (`instance`, `who-sets`, `who-gets`, `tests-using`, `bridge`) + a read-only MCP `query` tool | D0 |
| **D2** | Level-2 roots stored with read sets; incremental invalidation | elaboration note E1–E2 |
| **D3** | Model diff between two databases; a CI job that posts it on a PR | D2 |
| **D4** | Lint reads facets from the database when they are fresh; findings table | D2, the SV rule host |
| **D5** | PSS (pssparser's linked model) into the same database; cross-model views | rules-over-models §6.5 |

**D0–D1 are small and useful on their own.** sphinx-systemverilog already computes level 1, and
a queryable `who-sets`/`bridge` over it is a tool people would use before any interpreter
exists. That makes D0–D1 a cheap test of the "queryable database" value, in the same spirit as
the demand tests in `sv-lint-demand.md`.

## 5. Risks

| Risk | Mitigation |
|---|---|
| **The schema becomes a public API** the moment someone scripts against it | views are the API, versioned; base tables are documented as internal |
| **Stale answers** (an editor shows facts from before the last edit) | every row carries its inputs; the CLI checks freshness before answering and says when it rebuilt or what is stale |
| **False confidence** from dropping unknowns | §3.3: certainty, multiplicity, state and completeness are columns, and the views return them |
| **Two implementations of the model** (Python extraction for docs, C++ for the editor/WASM) | one producer writes the database; others read it. That is Q-EL6's shared package, with the database as its persistence |
| **Scope creep into a general code-intelligence platform** | start from the questions in §2 that nothing answers today (run-time structure); declarations and references are stored only as far as those queries need them |

## 6. Open questions

- **Q-DB1.** SQLite (recommended) or DuckDB?
- **Q-DB2.** Is the database **owned by the shared UVM model package** (Q-EL6), with lint and
  docs as readers, or by the lint host?
- **Q-DB3.** Should findings be stored (joinable with structure: "findings in components
  under `env.axi*`"), or stay in SARIF only?
- **Q-DB4.** Should a CI-published database per commit be part of the first version, or does
  the local cache come first?
- **Q-DB5.** Tool name and placement: a separate `svq` CLI, or subcommands of the SV checker
  (`svcheck query …`)?

## Precedents

- **CodeQL:** source is extracted into a relational database, then queried; the database is a
  shareable artifact per commit.
- **Glean (Meta):** a schema'd fact store with derived facts, incremental indexing, and the
  Angle query language.
- **Kythe (Google)** and **SCIP/LSIF (Sourcegraph):** cross-reference graphs computed once and
  served to editors and code browsers without the compiler.
- **Verdi KDB (Synopsys):** a persisted compiled and elaborated design database for debug
  queries. It covers the design and static elaboration, not a testbench's run-0 structure.
- **DVT runtime elaboration (AMIQ):** computes run-0 structure for one test into IDE views; not
  exposed as a queryable store, as far as public material shows (see the elaboration note's
  sources).
