"""The lint pipeline on SV (svcheck-plan P2.3; src/cli/{Lint,Config,Query}.cpp):
selection (profiles, `--select`/`--ignore`, `-Werror` and friends), `--config`
with per-path and per-context overrides and declared contexts (P2.4), source
waivers (`// svcheck waive`), slang's warnings as SLANG### markers (Q-UVM4), the
context on findings, the query modes (`--list-markers`, `--explain`,
`--check-rules`, `--rules-for`, `--show-config`), the GitHub format, vendor `.f`
options, rule directories and list interpolation in messages.
"""
import json
import os
import subprocess

import pytest

from svcheck import native_path
from test_cli import AGENT, DRIVER, ENV, RULES, UVM

pytestmark = pytest.mark.skipif(not os.path.isfile(os.path.join(UVM, "uvm_pkg.sv")),
                                reason="UVM is not in packages/ (ivpm update)")

# A rule with no applies_to, so a context override has something to narrow.
ANY_OBJECTION = """\
    - id: ACME003
      name: any-objection
      tier: suspicious
      summary: An objection
      message: "${node.subroutine.name}"
      match: { kind: Call }
      where: node.subroutine.name == "raise_objection"
"""

WARNS = """\
module m;
  logic [3:0] a;
  initial a = 8'hFF;
endmodule
"""


@pytest.fixture
def tb(tmp_path):
    (tmp_path / "agent").mkdir()
    (tmp_path / "agent" / "apb_agent_pkg.sv").write_text(AGENT)
    (tmp_path / "agent" / "apb_driver.svh").write_text(DRIVER)
    (tmp_path / "env_pkg.sv").write_text(ENV)
    (tmp_path / "warns.sv").write_text(WARNS)
    (tmp_path / "agent.f").write_text("+incdir+agent\nagent/apb_agent_pkg.sv\n")
    (tmp_path / "tb.f").write_text(
        "+incdir+%s\n%s\n-f agent.f\nenv_pkg.sv\n" % (UVM, os.path.join(UVM, "uvm_pkg.sv")))
    (tmp_path / "r.yaml").write_text(RULES + ANY_OBJECTION)
    return tmp_path


def svcheck(cwd, *args):
    return subprocess.run([native_path()] + list(args), cwd=cwd, capture_output=True,
                          text=True, timeout=120)


def found(r):
    """(code, severity, file, line) of each JSON finding."""
    out = json.loads(r.stdout)
    return [(f["code"], f["severity"], f["file"], f["line"]) for f in out["findings"]]


def codes(r):
    return [f[0] for f in found(r)]


def lint(tb, *args):
    return svcheck(tb, "-f", "tb.f", "--rules", "r.yaml", "--format", "json", *args)


# ------------------------------------------------------------ selection ----

def test_a_rule_file_runs_under_the_default_profile(tb):
    # Naming the file asks for its rules: they are in every profile but none.
    assert codes(lint(tb)) == ["ACME001", "ACME003", "ACME001", "ACME003"]
    assert codes(lint(tb, "--profile", "strict")) == codes(lint(tb))
    r = lint(tb, "--profile", "none")
    assert (r.returncode, codes(r)) == (0, [])


def test_select_and_ignore(tb):
    assert codes(lint(tb, "--ignore", "ACME003")) == ["ACME001", "ACME001"]
    assert codes(lint(tb, "--ignore", "ACME")) == []
    # By name; an ignore beats a select as specific.
    assert codes(lint(tb, "--ignore", "any-objection", "--ignore", "ACME001")) == []
    assert codes(lint(tb, "--select", "ACME001", "--ignore", "ACME001",
                      "--ignore", "ACME003")) == []
    assert codes(lint(tb, "--profile", "none", "--select", "ACME003")) == ["ACME003"] * 2


def test_an_unknown_selector_is_a_usage_error(tb):
    r = lint(tb, "--select", "ACME0001")
    assert r.returncode == 2
    assert "ACME0001" in r.stderr and "nothing was checked" in r.stderr


@pytest.mark.parametrize("args, sev", [
    ([], "warning"),
    (["-Werror"], "error"),
    (["-Werror=ACME001"], "error"),
    (["-W", "error=objection-in-agent"], "error"),
    (["-Werror", "-Wno-error=ACME001"], "warning"),
])
def test_warning_policy(tb, args, sev):
    r = lint(tb, "--ignore", "ACME003", *args)
    assert {f[1] for f in found(r)} == {sev}
    if sev == "error":
        assert json.loads(r.stdout)["findings"][0]["original_severity"] == "warning"


def test_no_warnings(tb):
    r = lint(tb, "--no-warnings")
    assert (r.returncode, codes(r)) == (0, [])


# -------------------------------------------------------------- waivers ----

def _waive(tb, line_text, comment):
    p = tb / "agent" / "apb_driver.svh"
    p.write_text(p.read_text().replace(line_text, line_text + " " + comment))


def test_a_source_waiver(tb):
    _waive(tb, "phase.drop_objection(this);", "// svcheck waive ACME001: the default sequence's")
    r = lint(tb, "--ignore", "ACME003")
    out = json.loads(r.stdout)
    assert [(f["code"], f["line"]) for f in out["findings"]] == [("ACME001", 5)]
    assert [(f["code"], f["line"], f["waiver"]) for f in out["waived"]] == [
        ("ACME001", 6, "the default sequence's")]
    t = svcheck(tb, "-f", "tb.f", "--rules", "r.yaml", "--ignore", "ACME003")
    assert t.stdout.splitlines() == [
        "agent/apb_driver.svh:5:5: warning: raise_objection in an agent [ACME001]"]
    assert t.stderr.splitlines()[-1] == "svcheck: 0 errors, 1 warning, 1 waived"
    t = svcheck(tb, "-f", "tb.f", "--rules", "r.yaml", "--ignore", "ACME003", "--show-waived")
    assert t.stdout.splitlines()[1] == ("agent/apb_driver.svh:6:5: warning: drop_objection in "
                                        "an agent [ACME001] [waived: the default sequence's]")


def test_a_waiver_alone_on_its_line_covers_the_next(tb):
    p = tb / "agent" / "apb_driver.svh"
    p.write_text(p.read_text().replace(
        "    phase.raise_objection(this);",
        "    // svcheck waive objection-in-agent, ACME003: drains the bus\n"
        "    phase.raise_objection(this);"))
    # The env's objection is not under the waiver.
    assert [(f[0], f[2], f[3]) for f in found(lint(tb))] == [
        ("ACME001", "agent/apb_driver.svh", 7), ("ACME003", "env_pkg.sv", 9)]


def test_a_waiver_that_matches_nothing_is_reported(tb):
    _waive(tb, "endtask", "// svcheck waive ACME001: stale")
    r = lint(tb, "--ignore", "ACME003")
    assert codes(r) == ["ACME001", "ACME001", "SVCHK202"]


def test_sarif_marks_waived_results_suppressed(tb):
    _waive(tb, "phase.drop_objection(this);", "// svcheck waive ACME001: ok")
    r = svcheck(tb, "-f", "tb.f", "--rules", "r.yaml", "--ignore", "ACME003",
                "--format", "sarif")
    results = json.loads(r.stdout)["runs"][0]["results"]
    assert [x.get("suppressions") for x in results] == [
        None, [{"kind": "inSource", "justification": "ok"}]]


# ------------------------------------------------------------- contexts ----

def test_findings_carry_their_context(tb):
    out = json.loads(lint(tb, "--ignore", "ACME003").stdout)
    assert out["findings"][0]["context"] == [
        {"axis": "unit", "label": "agent", "unit": "apb_agent_pkg"}]
    r = svcheck(tb, "-f", "tb.f", "--rules", "r.yaml", "--ignore", "ACME003",
                "--format", "sarif")
    res = json.loads(r.stdout)["runs"][0]["results"][0]
    assert res["properties"]["context"][0]["label"] == "agent"


# --------------------------------------------------------------- config ----

def _config(tb, text):
    (tb / "cfg.yaml").write_text(text)
    return svcheck(tb, "-f", "tb.f", "--config", "cfg.yaml", "--format", "json")


def test_config_rules_select_and_severity(tb):
    r = _config(tb, "svcheck:\n  rules: [r.yaml]\n  ignore: [ACME003]\n"
                    "  severity: {ACME001: error}\n")
    assert found(r) == [("ACME001", "error", "agent/apb_driver.svh", 5),
                        ("ACME001", "error", "agent/apb_driver.svh", 6)]


def test_config_overrides_by_path(tb):
    r = _config(tb, "svcheck:\n  rules: [r.yaml]\n  overrides:\n"
                    "    - files: ['agent/**']\n      ignore: [ACME003]\n")
    assert [(f[0], f[2]) for f in found(r)] == [
        ("ACME001", "agent/apb_driver.svh"), ("ACME001", "agent/apb_driver.svh"),
        ("ACME003", "env_pkg.sv")]


def test_config_overrides_by_context(tb):
    # sv-contexts.md §5.3: the env's objections are not ACME003's business.
    r = _config(tb, "svcheck:\n  rules: [r.yaml]\n  overrides:\n"
                    "    - context: env\n      ignore: [ACME003]\n")
    assert [(f[0], f[2]) for f in found(r)] == [
        ("ACME001", "agent/apb_driver.svh"), ("ACME003", "agent/apb_driver.svh"),
        ("ACME001", "agent/apb_driver.svh")]
    # One unit by name.
    r = _config(tb, "svcheck:\n  rules: [r.yaml]\n  overrides:\n"
                    "    - context: agent(apb_agent_pkg)\n      ignore: [ACME001, ACME003]\n")
    assert [(f[0], f[2]) for f in found(r)] == [("ACME003", "env_pkg.sv")]


def test_config_errors_stop_the_run(tb):
    r = _config(tb, "svcheck:\n  rules: [r.yaml]\n  selct: [ACME001]\n")
    assert r.returncode == 2
    assert "svcheck.selct: unknown key" in r.stderr
    r = _config(tb, "svcheck:\n  rules: [r.yaml]\n  overrides:\n    - ignore: [ACME001]\n")
    assert r.returncode == 2
    assert "'files' or 'context'" in r.stderr


PROVENANCE = """\
sv-rules:
  version: 1
  rules:
    - id: ACME010
      name: in-vip
      tier: suspicious
      summary: s
      message: "${context(node, 'provenance').labels}"
      applies_to: { provenance: vip }
      match: { kind: Package }
"""


def test_config_declares_a_provenance_axis(tb):
    # P2.4 (Q-CX2): third-party VIP is declared, not derived; the axis need
    # not be any classifier's.
    (tb / "p.yaml").write_text(PROVENANCE)
    (tb / "cfg.yaml").write_text("svcheck:\n  contexts:\n"
                                 "    - files: ['agent/**']\n      context: vip\n"
                                 "      axis: provenance\n")
    r = svcheck(tb, "-f", "tb.f", "--config", "cfg.yaml", "--rules", "p.yaml",
                "--format", "json")
    out = json.loads(r.stdout)
    assert [(f["code"], f["file"], f["message"]) for f in out["findings"]] == [
        ("ACME010", "agent/apb_agent_pkg.sv", "vip")]
    r = svcheck(tb, "-f", "tb.f", "--config", "cfg.yaml", "--rules", "p.yaml", "--contexts")
    assert "unit apb_agent_pkg: vip" in r.stdout
    assert "Declared (cfg.yaml: svcheck.contexts[0])" in r.stdout


# ------------------------------------------------------- slang warnings ----

def test_slang_warnings_are_markers(tb):
    base = ["-f", "tb.f", "warns.sv", "--rules", "r.yaml", "--ignore", "ACME", "--format",
            "json"]
    # Not in the default profile; selected by namespace, ID or name (Q-UVM4).
    assert codes(svcheck(tb, *base)) == []
    for sel in ["SLANG", "SLANG177", "constant-conversion"]:
        r = svcheck(tb, *base, "--select", sel)
        assert found(r) == [("SLANG177", "warning", "warns.sv", 3)], (sel, r.stderr)
    # slang's own -Wno- still silences it.
    assert codes(svcheck(tb, *base, "--select", "SLANG",
                         "-Wno-constant-conversion")) == []


def test_slang_warnings_take_waivers(tb):
    p = tb / "warns.sv"
    p.write_text(p.read_text().replace("8'hFF;", "8'hFF;  // svcheck waive constant-conversion: ok"))
    r = svcheck(tb, "-f", "tb.f", "warns.sv", "--rules", "r.yaml", "--ignore", "ACME",
                "--select", "SLANG", "--format", "json")
    out = json.loads(r.stdout)
    assert (out["findings"], [w["code"] for w in out["waived"]]) == ([], ["SLANG177"])


def test_explain_a_slang_marker(tb):
    r = svcheck(tb, "--explain", "constant-conversion")
    assert r.returncode == 0
    assert r.stdout.splitlines()[0] == "SLANG177 constant-conversion"
    assert "slang's warning -Wconstant-conversion" in r.stdout


# ---------------------------------------------------------- query modes ----

def test_list_markers(tb):
    r = svcheck(tb, "--rules", "r.yaml", "--list-markers")
    ids = [line.split()[0] for line in r.stdout.splitlines()]
    assert {"SVCHK202", "SLANG001", "ACME001", "ACME003"} <= set(ids)
    cat = json.loads(svcheck(tb, "--rules", "r.yaml", "--list-markers", "--format",
                             "json").stdout)
    assert cat["schema"] == "svcheck-catalog/1"


def test_explain(tb):
    r = svcheck(tb, "--rules", "r.yaml", "--explain", "objection-in-agent")
    assert r.returncode == 0
    lines = r.stdout.splitlines()
    assert lines[0] == "ACME001 objection-in-agent"
    assert "  applies to: unit: [\"agent\"] (experimental)" in lines
    assert "  defined in: r.yaml:13" in lines
    r = svcheck(tb, "--rules", "r.yaml", "--explain", "ACME01")
    assert r.returncode == 2
    assert "did you mean 'ACME001'?" in r.stderr


def test_check_rules(tb):
    (tb / "ok.yaml").write_text("""\
sv-rules:
  version: 1
  rules:
    - id: ACME020
      name: no-pkg-x
      tier: style
      summary: s
      message: m
      match: { kind: PackageDeclaration }
      where: node.header.name.text == "x"
      examples:
        - code: |
            package x; endpackage   // expect: ACME020
        - code: |
            package y; endpackage   // expect: ACME020
""")
    r = svcheck(tb, "--rules", "ok.yaml", "--check-rules")
    assert r.returncode == 1
    assert "1 example passed, 1 failed" in r.stderr
    assert "ACME020" in r.stdout
    assert svcheck(tb, "--check-rules").returncode == 2


def test_rules_for_and_show_config(tb):
    (tb / "cfg.yaml").write_text("svcheck:\n  rules: [r.yaml]\n  overrides:\n"
                                 "    - files: ['env_pkg.sv']\n      ignore: [ACME003]\n")
    r = svcheck(tb, "--config", "cfg.yaml", "--rules-for", "env_pkg.sv",
                "agent/apb_agent_pkg.sv")
    assert r.returncode == 0, r.stderr
    rows = dict(line.split(": ", 1) for line in r.stdout.splitlines())
    assert "ACME003" not in rows["env_pkg.sv"] and "ACME003" in rows["agent/apb_agent_pkg.sv"]
    r = svcheck(tb, "--config", "cfg.yaml", "--show-config")
    assert json.loads(r.stdout)["ok"] is True


# ------------------------------------------------------- output, inputs ----

def test_github_format(tb):
    r = svcheck(tb, "-f", "tb.f", "--rules", "r.yaml", "--ignore", "ACME003",
                "--format", "github")
    assert r.stdout.splitlines()[0] == ("::warning file=agent/apb_driver.svh,line=5,col=5,"
                                        "title=ACME001::raise_objection in an agent")


def test_statistics(tb):
    r = svcheck(tb, "-f", "tb.f", "--rules", "r.yaml", "--statistics")
    assert r.stdout.splitlines()[-2:] == ["     2  ACME001", "     2  ACME003"]


def test_vendor_file_list_options_are_ignored(tb):
    (tb / "vendor.f").write_text("-sverilog -full64 +v2k -timescale=1ns/1ps\n"
                                 "--cc --exe -o sim -Wno-fatal -sv -64bit\n-f tb.f\n")
    r = svcheck(tb, "-f", "vendor.f", "--rules", "r.yaml", "--format", "json")
    assert json.loads(r.stdout)["compilation"]["errors"] == 0, r.stderr
    assert len(found(r)) == 4


OOB = """\
module m;
  bit [3:0] w;
  bit [7:0] b;
  int j;
  initial b = w[j*8 +: 8];
endmodule
"""


def test_no_error_reaches_slang_for_its_options(tb):
    # range-width-oob is an error slang lets be downgraded: -Wno-error= is svcheck's, and
    # also slang's when it names a slang option (lrm-relaxations, W).
    (tb / "oob.sv").write_text(OOB)
    r = svcheck(tb, "oob.sv")
    assert r.returncode == 1 and "error: cannot select range" in r.stderr, r.stderr
    r = svcheck(tb, "oob.sv", "-Wno-error=range-width-oob")
    assert r.returncode == 0 and "warning: cannot select range" in r.stderr, r.stderr
    r = svcheck(tb, "oob.sv", "-Wno-error=range-width-oob", "--select", "range-width-oob",
                "--format", "json")
    assert found(r) == [("SLANG104", "warning", "oob.sv", 5)], r.stderr


HIER_CONST = """\
module sub #(parameter int P = 1) (); endmodule
module core; localparam int En = 1; endmodule
module top;
  core u_core();
  sub #(.P(u_core.En)) u_sub();
endmodule
"""


def test_relax_lrm_allows_hierarchical_constants(tb):
    # slang's own --allow-hierarchical-const is one of --relax-lrm's (OpenTitan's kmac binds).
    (tb / "hc.sv").write_text(HIER_CONST)
    assert svcheck(tb, "hc.sv").returncode == 1
    r = svcheck(tb, "hc.sv", "--relax-lrm")
    assert r.returncode == 0 and "error" not in r.stderr, r.stderr


def test_a_rule_directory_loads_its_rule_files(tb):
    (tb / "rules").mkdir()
    (tb / "rules" / "a.yaml").write_text(RULES)
    (tb / "rules" / "helpers.yaml").write_text(
        "sv-rules:\n  version: 1\n  define:\n    h(x): x\n")
    (tb / "rules" / "notes.yaml").write_text("title: not rules\n")
    r = svcheck(tb, "-f", "tb.f", "--rules", "rules", "--format", "json")
    assert r.returncode == 1, r.stderr
    assert codes(r) == ["ACME001", "ACME001"]


def test_a_message_interpolates_a_list(tb):
    (tb / "l.yaml").write_text("""\
sv-rules:
  version: 1
  rules:
    - id: ACME030
      name: names
      tier: style
      summary: s
      message: "${['a', 'b'].map(x, x + node.header.name.text.substring(0, 3))}"
      match: { kind: PackageDeclaration }
      where: node.header.name.text == "env_pkg"
""")
    r = svcheck(tb, "-f", "tb.f", "--rules", "l.yaml", "--format", "json")
    assert [f["message"] for f in json.loads(r.stdout)["findings"]] == ["aenv, benv"]
