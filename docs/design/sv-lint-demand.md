# Declarative SV lint: would anyone come?

Status: research report, 2026-10-10. Nothing here is decided.
Follow-up to `sv-lint-survey.md`, which found that no SystemVerilog linter lets users write
**semantic** rules as data. This report asks whether anyone wants one, and looks for a use
beyond fw-hdl that would make it compelling.

Method: five web research passes on 2026-10-10, covering:

- HDL community demand: GitHub issues, forums, DVCon papers, in-house checkers.
- Declarative rule engines outside hardware.
- The size of the SV tooling market.
- Four candidate new uses, two per pass.

GitHub numbers come from the GitHub API on that date. Figures marked *(unconfirmed)* come
only from search-engine summaries.

## 0. Answer

> **Not for the rule format alone.** No one in the HDL world is asking for "rules as data".
>
> - We found 8 explicit requests for custom rules or plugins across Verible, svlint and
>   Verilator in 15 years. They have about 3 reactions in total, and none got pushback when
>   the maintainers declined.
> - svlint's plugin system has one visible user.
> - New rules in Verible have nearly stopped.
>
> Demand for the format would have to be **created**. Built alone, it would draw the
> fw-hdl-sized audience the survey already expected.
>
> **There is real, paid-for demand for custom rules inside companies:**
>
> - Intel (SVA lint, 2012) and Lightelligence (`draconian_uvm`) built their own checkers.
> - AsFigo's family of "Build Your Own Linter" tools exists because users roll their own.
> - SpyGlass, Verissimo and ALINT-PRO all sell rule APIs.
> - The one public paper on the topic says "many customers … develop their own rules/policies
>   primarily based on UVM".
>
> That demand is served today by hand-written pyslang scripts, Perl decks and a Java API. It
> is an audience to win over, not one that is asking for us.
>
> **The new use that makes it compelling is guardrails for AI agents writing SystemVerilog**
> (§3.1). Every condition that made a declarative rule engine succeed elsewhere is present
> there:
>
> - a fast-growing, well-funded user base: ChipAgents' Series A totals $134M across 120+
>   semiconductor customers, and Cognichip raised $60M;
> - a proven pattern in software: conventions written in AGENTS.md get turned into lint
>   rules;
> - a gap: no MCP server for HDL offers user-defined rules;
> - a format that suits the job: bounded YAML+CEL rules are what an LLM can draft and a person
>   can review.
>
> fw-hdl stops being the product and becomes one instance of a general pattern.
>
> **Recommended shape:**
>
> - lead with a built-in **UVM/methodology rule pack** on slang (§3.5); open testbench lint
>   barely exists;
> - deliver it as an **agent guardrail** over MCP and in CI;
> - treat "write your own rule as YAML" as the second feature;
> - treat **integration contracts shipped with IP** (§3.2) as the generalisation of fw-hdl, to
>   prove out on open IP (Caliptra, OpenTitan).
>
> **Cheap tests** before building anything are in §5.

## 1. Direct demand from the HDL community

### 1.1 What people ask for

| Source | Ask | Response |
|---|---|---|
| [verible#1999](https://github.com/chipsalliance/verible/issues/1999), 2023 | rules private to an organisation; a later commenter asks for a clang-tidy-style plugin system | "no plug-in system", contribute upstream. Open, 1 reaction, 4 comments |
| [verilator#6243](https://github.com/verilator/verilator/issues/6243), 2025 | "I have many custom rules" | "There isn't a 'rule language' … write code in C++" |
| [svlint#248](https://github.com/dalance/svlint/issues/248), 2023 | one user bundling several plugin rules | merged; that user is the only visible plugin author |
| [svlint#237](https://github.com/dalance/svlint/issues/237), 2023 | rule tiers up to elaborated rules ("elabrules") | "long term", still open |
| verible-dev list (2020), verible #133, #519, #902; verilator#310 | external rules, configurable rules, rule aliases | parameters added (#133); others open or answered with built-in options |

- **Most of the visible pull is for configuring naming rules**, not for a way to add new rules:
  - an `m_` prefix for class members: svlint#178, verible#1357;
  - port prefixes: verible#2490 (2026);
  - generate-label prefixes: verible#2288.
- **Regex options already answer most of it.** OpenTitan, among the best-resourced open
  hardware groups, got everything it needed from making Verible's rules configurable
  (opentitan#1342).
- **slang has had no request for custom rules.** Its one AST-matcher PR (#1376, 2025) was
  closed as an unfinished draft.

### 1.2 Counter-evidence

- **svlint's plugin system goes unused.** `svlint-plugin-sample` has 4 stars, against 393 for
  svlint, and its last push was 2023-10.
- **New Verible rules have nearly stopped.** By year: 12 (2020), 8, 1, 1, 3 (2024). None since
  2025-01; the commits since then are fixes and new options.
- **slang-tidy is the one growing linter:** 38 checks, with 13 new in 2026, all upstream C++
  contributions. Nobody has asked it for plugins.

### 1.3 Where the real demand is

- **Companies write their own rules.**
  - [Intel, DVCon 2012](https://dvcon-proceedings.org/wp-content/uploads/systemverilog-assertion-linting-closing-potentially-critical-verification-holes.pdf):
    derived SVA lint rules from its own bug escapes because commercial tools lacked them.
  - [Lightelligence/draconian_uvm](https://github.com/Lightelligence/draconian_uvm): a
    company's extra guidelines on top of UVM.
  - [BTA tb_lint](https://github.com/BTA-design-services/tb_lint): a services company's
    testbench linter.
  - [AsFigo's BYOL family](https://github.com/AsFigo/pyslint): pyslint, svalint, yoYoLint,
    fcovlint and others, each with tens of stars or none.
  - [PySlint DVCon paper](https://dvcon-proceedings.org/wp-content/uploads/92146.pdf): "UVM
    rules are developed on customer specific requirements", and pyslang rule-writing is
    "trial-and-error".
- **Vendors sell rule-writing APIs:** SpyGlass (C/Perl), Verissimo (Java), ALINT-PRO (C++).
  None of them leads its marketing with custom rules. The products sell curated rule decks
  plus policy configuration.
- **Job postings:** none ask specifically for rule development. One Meta posting reportedly
  mentions "structural rule based checks" *(unconfirmed, page returned 403)*.

**Reading.** Demand for semantic and methodology rules, as opposed to naming rules, exists in
verification teams and is paid for. It is met with code that is expensive and fragile to
write. Nobody has asked for a data format, so the pitch must be "replace your pyslang scripts
and Perl decks", not "a rule language".

## 2. What declarative rule engines outside hardware teach

| Case | Evidence | Lesson |
|---|---|---|
| **Kyverno vs OPA Gatekeeper** | Kyverno (YAML policies) graduated in CNCF 2026-03-24, reporting 3B downloads; 8.2k stars vs Gatekeeper's 4.3k. Kyverno 1.14 (2025-04) made **CEL** the main language for new policies, following Kubernetes' own ValidatingAdmissionPolicy | **YAML + CEL is where Kubernetes policy settled.** The simpler, less expressive format won its market. The limit people hit is that CEL cannot query other objects, so it needs a rich precomputed model to query. We have that model: the elaborated AST plus library functions |
| **CodeQL model packs** | GitHub moved library modelling (sources, sinks, summaries) out of QL code into YAML "data extensions", deployable org-wide | Even a code-first tool moved the part ordinary users change into data. This matches our split: a closed function library, with the rules as data |
| **Semgrep** | $100M Series D (2025-02); 2–3k community rules, 20k+ Pro rules. In its own words, most security teams "lack the bandwidth to write or customize rules … even with Semgrep's simple rule syntax"; its AI direction is "no YAML required" | Easy rule writing spreads a tool. **Revenue comes from curated rules plus a buyer with a mandate** (security) |
| **ast-grep** | 16k stars, doubled in 18 months; ~2M npm downloads a week; official MCP server; used by CodeRabbit | Recent growth plausibly comes from **AI agents** using it as a tool *(inference)* |
| **Biome / GritQL**, **Ruff** | Biome 2.0 chose a query language over JavaScript for plugins. Ruff (50k stars) has no plugins and reimplements popular rules natively | A large built-in rule set can win on its own. A bounded query language is what native-code linters choose |
| **Rules shipped with libraries** | `eslint-plugin-react-hooks` 98M downloads a week (ESLint itself: 162M); `xunit.analyzers` 1.07B total; Android `lintPublish` in AARs | The pattern is large and real, **but almost always code**. Automatic pickup depends on a package manager, and SV has none that is universal (FuseSoC and IVPM are partial). **We found no mainstream case of a library shipping data rules that are picked up automatically.** That part is new |
| **Safety / no trust prompt** | VS Code ESLint asks before running workspace code. The 2025 `eslint-config-prettier` compromise ran from an install script, not at lint time | Real but secondary; nobody chooses a linter for it. It matters most for **agents and sandboxed hosts** (§3.1), which run third-party rules without a person to approve them |
| **LLMs** | Factory (2025-09): conventions go from AGENTS.md into custom ESLint rules; LLMs draft the rules. QLCoder: Claude Code alone wrote a correct CodeQL query for 10% of CVEs, 53% with tool feedback | LLMs make code-based rules easier to write, which narrows the format's authoring advantage. They raise the value of **rules a person can review and a schema can validate** |

**Rules that agents read, collected from dependencies.**
[ash-project/usage_rules](https://github.com/ash-project/usage_rules) (Elixir, 227 stars,
2025-05 to now) collects `usage-rules.md` files shipped by dependencies into a project's
`AGENTS.md` and agent skills. It is the distribution and aggregation half of §3.1 and §3.2,
already working in another ecosystem, but its rules are prose. Our addition is rules that are
also checked.

**What made the winners succeed:** a large existing platform, a buyer with a mandate, a large
curated rule set that works without writing anything, and distribution through an existing
channel. A rule format alone has never been enough.

## 3. Candidate new uses

### 3.1 Guardrails for AI agents writing SystemVerilog — **strong**

- **Lint feedback is already standard in agent loops.**
  - AutoChip (2023) and Blocklove et al. (*TODAES* 2025) feed EDA tool output back to the
    model.
  - RTLBench (2025-11): most of 24 LLMs fall short on lint compliance and style. Feeding lint
    logs back cut violations by up to 229.
  - A synthesis-in-the-loop study (2603.11287) found open-weight models fail early on
    undefined modules (40%) and non-synthesisable constructs (29%).
  - "Unsaid, Unsafe?" (2608.26588): frontier models pass functional tests in 73–79% of cases
    but security tests in only 14–35%.
- **Vendors are positioning lint as the deterministic layer under agents.**
  - Sigasi 2026.2 shipped an MCP server that hands agents "project rules". Its 2026-07 blog
    calls lint "the deterministic substrate that half the agentic layer quietly stands on".
  - AMIQ DVT shipped an MCP server (2026-01).
  - Open MCP servers exist: `mcp-verilog` (Verible), `pyslang-mcp`, `verilog-mcp-server`.
    **None offers user-defined rules.**
- **The money is there.** ChipAgents: $134M Series A, 120+ semiconductor customers. Cognichip:
  $60M. Cadence ChipStack and Synopsys AgentEngineer now generate RTL.
- **Software shows how it goes:** "your AI agent ignored AGENTS.md; your linter won't let it."
  Conventions move from prompt files into lint rules.
- **Why rules as data fit here specifically:**
  1. An agent or a reviewer can draft a rule from a review comment, and a person can review
     ten lines of YAML. Reviewing generated C++ against slang's API is a different job.
  2. Rules load into an agent's sandbox or an MCP server with no code execution. The safety
     argument that was secondary for people (§2) is primary here.
  3. Findings are the same in the agent loop, the editor and CI.
  4. Methodology conventions such as UVM and fw-hdl's are exactly what agents get wrong and
     what prompts state only loosely.
- **Who writes rules:** methodology and CAD teams, with agents drafting. **Who pays:** CAD
  infrastructure budgets, or agent startups embedding the engine.
- **Strongest counter-argument:**
  - The incumbents are already in the loop. Sigasi, Verissimo and SpyGlass have rules plus
    MCP or AI hooks, and agent vendors will bundle their own checkers.
  - The real bottleneck for agents is functional correctness, which simulation and formal
    verification measure, not style. A lint engine alone risks being a feature, not a product.
  - We found **no direct statement** from anyone wanting rules drafted by an LLM as YAML for
    HDL. This is extrapolated from software.

### 3.2 Integration contracts shipped with IP — **moderate**

This is fw-hdl's idea, generalised. An IP block ships checkable rules about how it must be
instantiated: legal parameter combinations, required tie-offs, connection constraints.

- **The pattern already exists in proprietary form:**
  - Synopsys coreConsultant gives each DesignWare parameter a `CheckExpr` Tcl rule with a
    message.
  - SpyGlass CDC abstract models (SGDC) are constraint files handed up with an IP.
  - SpyGlass IP Kit / TSMC Soft IP9000 (2012): 15+ IP suppliers, including Arm.
  - IP-XACT parameter constraints, plus the Arteris/Magillem checker suites.
  - Defacto SoC Compiler's integration checks.
- **Vendors do ship machine-checkable collateral**, but it is locked to one vendor's tools,
  and most of it checks the IP's configuration rather than the integrator's RTL.
- **Open IP still writes these rules as prose.** The
  [Caliptra Integration Spec](https://github.com/chipsalliance/caliptra-rtl/blob/main/docs/CaliptraIntegrationSpecification.md)
  says single-iTRNG builds "shall tie itrng1_en/data/valid to 0", and that undefined
  GENERIC_INPUT_WIRES bits must be tied to 0 and not reach flops. Caliptra issue #751 shows
  an integrator unable to tell how to meet a requirement.
- **Integration bugs are a real cost.** DVCon has a run of formal-connectivity papers, and a
  bad connection "can result in a re-spin". But Wilson 2022 says external IP contributes
  *less* to functional flaws than design errors. We found no figure that isolates
  integration bugs.
- **Who writes rules:** IP and open-IP maintainers. **Who benefits:** integrators, and the
  vendor's support budget.
- **Strongest counter-argument:**
  - Commercial vendors standardise on Big-3 collateral, which their customers' sign-off flows
    already consume.
  - The costly integration bugs are CDC/RDC, power and physical, which structural rules
    don't reach.
  - **The opening is open IP:** Caliptra, OpenTitan, CORE-V and PULP, whose integrators often
    have no SpyGlass licence. That is also where fw-hdl lives.

### 3.3 Hardware security rules — **moderate, as a rule pack rather than a product**

- **Catalogue:** NIST IR 8517 (Feb 2024) counts 108 hardware CWEs in 13 categories. MITRE's
  2025 list of the most important hardware weaknesses includes debug access control (1191),
  debug mode overriding locks (1234) and register-interface access control (1262), all
  partly checkable in RTL structure.
- **Market:**
  - Caspia CODAx: 150+ checks, $4M raised.
  - Cycuity: launched a static product (Radix-ST) in 2025-08; Arteris bought it for up to $45M
    (closed 2026-01).
  - Cycuity's Radix users **write their own rules about their own assets**. That is the
    strongest custom-rule evidence in this report, but the rules are proprietary and run
    dynamically.
- **Driver:** the EU Cyber Resilience Act. Reporting started 2026-09-11 and full conformity
  applies from 2027-12. Arteris frames it as needing **evidence** of which assets were checked
  against which weakness categories. Deterministic rules mapped to CWEs fit that need.
- **Results:** the 2024–25 Hack@DAC/CHES lessons (2608.22202) say lint found real bugs only
  after **warnings were prioritised on key, privilege and lock signals**. Generic rules need
  asset context the organisation supplies. Precision of tuned academic tools (CWEEP) is
  51–61%.
- **Strongest counter-argument:** the important hardware CWEs are about information flow, and
  pattern rules are noisy. The commercial niche is small: the leader sold for at most $45M.
- **Fit:** a later rule pack on the same engine (CWE base rules plus asset annotations), not
  the lead.

### 3.4 Rule packs for tool portability — **weak**

The idea is a pack that answers "will this compile on Verilator, or synthesise in Yosys?".

- **Arguments for it:**
  - The shift to open tools is real. Verilator elaborates upstream UVM (2025-10).
  - OpenTitan opened a proposal this week to make Verilator a sign-off linter (#31673,
    2026-10-08), because open contributors cannot run AscentLint.
  - sv-tests already publishes support for each feature, tool by tool.
- **Against it:**
  - The cheapest portability check is to run the free tool, which OpenTitan and PULP CI
    already do.
  - Packs go stale with every monthly Verilator release.
  - Documented subsets are wrong: AMD's forum admits UG901 does not match Vivado.
- **The niche that remains is silent divergence.** Verilator silently drops covergroup crosses
  that Questa accepts (riscv-arch-test#2484). That needs expert rules tied to tool versions:
  a contribution, not a product.

### 3.5 Lint for testbench code — **the gap the other uses build on**

- **The gap:**
  - The PySlint paper: "on the verification side, lint usage has been limited in adoption",
    and there are "no ready-made/standard rule policies".
  - Verissimo (about 700 rules, Java custom-rule API) is the only dedicated product.
  - Sigasi ships a few UVM rules and no rule language.
  - Open tools have nothing usable: pyslint has 33 stars, and its last push was 2025-05.
- **Rule lists that are ready to encode:**
  - Doulos Easier UVM: 100+ guidelines in 19 categories, many statically checkable.
  - The Verification Academy UVM cookbook guidelines.
  - core-v-verif's `CodingStyleGuidelines.md`.
  - lowRISC's DV style guide.
  - Sutherland's "UVM Rapid Adoption" subset.
- **Audience:** UVM is used on about 80% of ASIC projects (Wilson 2024), and peak verification
  teams average about 14 engineers.
- **Substrate:** pyslang gets about 740k downloads a month (mostly automated installs). It is
  becoming the default SV front end, and our host would sit on the same engine.

## 4. Market size, honestly

- **Open SV tooling users:** low tens of thousands at most.
  - Verible has 2.0k stars, slang 1.2k, svlint 393.
  - The official Verible VS Code extension has 27k installs.
  - The catch-all `mshr-h.VerilogHDL` extension (1.5M installs) is the de facto front door,
    and it wraps whichever linter is installed.
- **Realistic reach for a slang-based methodology linter:** hundreds to low thousands of
  teams, or about 10³–10⁴ engineers, within a few years.
- **Library authors who would ship rule packs:** 10¹–10². They are a distribution channel
  (UVM, Caliptra, OpenTitan, CORE-V, in-house base classes), not the market.
- **Independent HDL lint businesses are small.** Linty Services (SonarQube-based) is estimated
  at about $220k ARR. Nobody has visibly commercialised Verible or svlint.

This is not a venture-scale product on its own. As open infrastructure with an AI-agent
wedge, it is a credible project with a real audience.

## 5. Recommendations

### 5.1 Changes to `sv-lint-survey.md`

- **§5.1 (keep the kernel language-neutral):** unchanged. It is cheap, and every option here
  needs it.
- **§0 "what it offers" and §5.5 "positioning":** reorder.
  1. Lead with **a built-in UVM/methodology pack, run as a deterministic guardrail for agents
     and in CI**.
  2. Second: rules written by teams and drafted by an LLM.
  3. Third: rules carried by libraries and IP.
  - Present safety (no code execution) as what makes rules loadable in an agent, not as a
    benefit to people.
- **§5.3 spike:** change the rule mix and add an agent measurement.
  - **Rules:** 3 Verible rules (enough to check equivalence), 4 Easier-UVM/cookbook rules,
    2 fw-hdl rules, and 2 Caliptra integration rules.
  - **Expose the engine as an MCP tool**, then measure one thing: does an agent's generated
    UVM converge on the team's conventions faster with the rules than with the same
    conventions stated in a prompt?
  - That measurement is the go/no-go for the product thesis, not only for the engineering.
- **New open question Q-SV5:** distribution. SV has no package manager that picks up rules
  automatically. Decide whether IVPM/FuseSoC metadata or an explicit `rules:` entry in
  `flow.yaml` carries them.

### 5.2 Cheap tests of demand, before building

1. **Caliptra paper exercise (1–2 days).** Encode the "shall" statements in the Caliptra
   integration spec as `sv-rules:` YAML. Count how many fit and how many need a new library
   function. Show the result to the Caliptra maintainers (CHIPS Alliance) and record whether
   they would carry it in the repo.
2. **UVM pack paper exercise (2–3 days).** Encode 20 Easier-UVM/cookbook guidelines. This
   checks the claim that methodology rules fit in about 15 lines, and gives a demo artifact.
3. **Ask the agent vendors (low cost).** A short note to one or two agent teams (ChipAgents,
   Cognichip) or to the `pyslang-mcp` maintainer: would a rule engine that can be loaded
   without running code, with team-written rules, be useful in their loop? A "no, we bundle
   our own" answer is worth knowing early.
4. **OpenTitan #31673 (watch).** It is a live discussion about running open-tool lint at
   sign-off. If it lands, open-IP projects will need waivers, baselines and per-IP rules,
   which our kernel already has.

If tests 1 and 3 both come back negative, stop at §5.1 of the survey and keep the SV host as
an fw-hdl-only tool.

## 6. Naming (2026-10-10)

- **`sv-rules`** is the specification: the rule-file format, with root key `sv-rules:`,
  parallel to `pss-rules:`.
- **`svcheck`** is the slang-based tool.
- Both are free on PyPI as of 2026-10-10. The `svcheck`, `sv-check` and `svchecker`
  spellings are all free.
- GitHub has only small, unrelated `svcheck` repositories: IBM Spectrum Virtualize (10 stars),
  a Solidity checker (7 stars) and a server-version CLI (3 stars).
- Neither name is reserved yet.
- Positioning: as of 2026-10-10, rule authoring is treated as the weaker part of the value.
  Distribution, discovery and aggregation of rules shipped with libraries and IP, and guiding
  AI tools to follow a project's local style, are the stronger part.

## 7. Sources

Inline above. Additional sources:

- [Kyverno CNCF graduation](https://www.cncf.io/announcements/2026/03/24/cloud-native-computing-foundation-announces-kyvernos-graduation/)
- [Kyverno 1.14 / CEL](https://www.cncf.io/blog/2025/05/14/announcing-kyverno-release-1-14/)
- [CodeQL model packs](https://github.blog/changelog/2024-04-16-configure-organization-level-codeql-model-packs-for-github-code-scanning/)
- [Semgrep on custom rules](https://semgrep.dev/blog/2025/why-ai-powered-memories-are-the-future-of-semgrep-sast/)
- [ast-grep: YAML vs DSL](https://ast-grep.github.io/blog/yaml-vs-dsl)
- [Factory: linters direct agents](https://factory.ai/news/using-linters-to-direct-agents)
- [QLCoder](https://arxiv.org/abs/2511.08462)
- [Sigasi: can AI generate HDL](https://www.sigasi.com/news/can_ai_generate_hdl/)
- [AMIQ DVT MCP](https://eda.amiq.com/press-releases/amiq-eda-gives-ai-agents-access-to-essential-design-and-verification-data)
- [pyslang-mcp](https://github.com/kammoh/pyslang-mcp)
- [Caspia CODAx](https://caspiatechnologies.com/codax/)
- [Arteris–Cycuity](https://www.marketscreener.com/news/arteris-inc-definitive-agreement-to-acquire-cycuity-inc-for-45-million-ce7d50d8da8bf021)
- [CRA as evidence](https://www.design-reuse.com/blog/56434-the-cyber-resilience-act-is-turning-hardware-security-into-an-evidence-question/)
- [MITRE 2025 hardware weaknesses](https://cwe.mitre.org/topHW/archive/2025/2025_CWE_MIHW.html)
- [Synopsys coreTools](https://www.synopsys.com/dw/doc.php/ds/o/coretools_ds.pdf)
- [OpenTitan lint](https://opentitan.org/book/hw/lint/index.html)
- [Verilator upstream UVM](https://antmicro.com/blog/2025/10/support-for-upstream-uvm-2017-in-verilator)
- [Easier UVM guidelines](https://www.doulos.com/knowhow/systemverilog/uvm/easier-uvm/easier-uvm-coding-guidelines/summary-of-the-easier-uvm-coding-guidelines/)
- [Wilson 2024](https://resources.sw.siemens.com/en-US/white-paper-2024-wilson-research-group-ic-asic-functional-verification-trend-report/)
