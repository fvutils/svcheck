"""The lint command line (svcheck-plan P1.5; src/cli/Lint.cpp, src/host/Lint.cpp):
`svcheck --rules FILE` over the compilation slang's driver makes of `-f`,
`+incdir+` and `+define+` (R-4); text, JSON and SARIF output; rules reported
**not run** when the compilation they need has errors (design §3.3);
`applies_to`; `--contexts`; `--library-dir`.
"""
import json
import os
import subprocess

import pytest

from svcheck import native_path

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
UVM = os.path.join(ROOT, "packages", "uvm-core", "src")
pytestmark = pytest.mark.skipif(not os.path.isfile(os.path.join(UVM, "uvm_pkg.sv")),
                                reason="UVM is not in packages/ (ivpm update)")

AGENT = """\
package apb_agent_pkg;
  import uvm_pkg::*;
  `include "uvm_macros.svh"
  `include "apb_driver.svh"
  class apb_agent extends uvm_agent;
    `uvm_component_utils(apb_agent)
    function new(string name, uvm_component parent); super.new(name, parent); endfunction
  endclass
endpackage
"""

DRIVER = """\
class apb_driver extends uvm_driver #(uvm_sequence_item);
  `uvm_component_utils(apb_driver)
  function new(string name, uvm_component parent); super.new(name, parent); endfunction
  task run_phase(uvm_phase phase);
    phase.raise_objection(this);
    phase.drop_objection(this);
  endtask
endclass
"""

ENV = """\
package env_pkg;
  import uvm_pkg::*;
  import apb_agent_pkg::*;
  `include "uvm_macros.svh"
  class my_env extends uvm_env;
    `uvm_component_utils(my_env)
    function new(string name, uvm_component parent); super.new(name, parent); endfunction
    task run_phase(uvm_phase phase);
      phase.raise_objection(this);
      phase.drop_objection(this);
    endtask
  endclass
endpackage
`ifdef WITH_EXTRA
package extra;
endpackage
`endif
"""

RULES = """\
sv-rules:
  version: 1
  contexts:
    - label: agent
      axis: unit
      carrier: Package
      when: '!carrier.inLibrary && classes(carrier).exists(c, derives_from(c, "uvm_pkg::uvm_agent"))'
    - label: env
      axis: unit
      carrier: Package
      when: '!carrier.inLibrary && classes(carrier).exists(c, derives_from(c, "uvm_pkg::uvm_env"))'
  rules:
    - id: ACME001
      name: objection-in-agent
      tier: suspicious
      summary: An agent raises or drops an objection
      message: "${node.subroutine.name} in an agent"
      applies_to: { unit: agent }
      match: { kind: Call }
      where: node.subroutine.name in ["raise_objection", "drop_objection"]
    - id: ACME002
      name: package-suffix
      tier: suspicious
      summary: A package name does not end in _pkg
      message: "package '${node.header.name.text}' does not end in _pkg"
      match: { kind: PackageDeclaration }
      where: '!node.header.name.text.endsWith("_pkg")'
"""


@pytest.fixture
def tb(tmp_path):
    """A testbench: an agent (its driver `include`d), an env, and nested .f files."""
    (tmp_path / "agent").mkdir()
    (tmp_path / "agent" / "apb_agent_pkg.sv").write_text(AGENT)
    (tmp_path / "agent" / "apb_driver.svh").write_text(DRIVER)
    (tmp_path / "env_pkg.sv").write_text(ENV)
    (tmp_path / "agent.f").write_text("+incdir+agent\nagent/apb_agent_pkg.sv\n")
    (tmp_path / "tb.f").write_text(
        "+incdir+%s\n%s\n-f agent.f\nenv_pkg.sv\n" % (UVM, os.path.join(UVM, "uvm_pkg.sv")))
    (tmp_path / "r.yaml").write_text(RULES)
    return tmp_path


def svcheck(cwd, *args):
    return subprocess.run([native_path()] + list(args), cwd=cwd, capture_output=True,
                          text=True, timeout=120)


def test_text_findings_at_the_include_and_rc_1(tb):
    r = svcheck(tb, "-f", "tb.f", "--rules", "r.yaml")
    assert r.returncode == 1, r.stderr
    assert r.stdout.splitlines() == [
        "agent/apb_driver.svh:5:5: warning: raise_objection in an agent [ACME001]",
        "agent/apb_driver.svh:6:5: warning: drop_objection in an agent [ACME001]",
    ]
    # slang's warnings are not findings; uvm_pkg is library code, not linted.
    assert r.stderr.splitlines() == ["svcheck: 0 errors, 2 warnings"]


def test_defines_reach_the_compilation(tb):
    r = svcheck(tb, "-f", "tb.f", "+define+WITH_EXTRA", "--rules", "r.yaml")
    assert "env_pkg.sv:15:9: warning: package 'extra' does not end in _pkg [ACME002]" in (
        r.stdout.splitlines())


def test_no_findings_is_rc_0(tb):
    (tb / "r.yaml").write_text(RULES.replace('"raise_objection", "drop_objection"', '"nope"'))
    r = svcheck(tb, "-f", "tb.f", "--rules", "r.yaml")
    assert (r.returncode, r.stdout) == (0, "")


def test_json(tb):
    r = svcheck(tb, "-f", "tb.f", "--rules", "r.yaml", "--format", "json")
    assert r.returncode == 1
    doc = json.loads(r.stdout)
    assert [(f["code"], f["file"], f["line"], f["col"]) for f in doc["findings"]] == [
        ("ACME001", "agent/apb_driver.svh", 5, 5), ("ACME001", "agent/apb_driver.svh", 6, 5)]
    assert doc["compilation"] == {"errors": 0, "unparsed": [], "files": [
        "agent/apb_agent_pkg.sv", "agent/apb_driver.svh", "env_pkg.sv"]}
    stats = {s["id"]: s for s in doc["rules"]}
    # The env's calls (its own and its macros') are outside the agent.
    assert stats["ACME001"]["findings"] == 2 and stats["ACME001"]["outside"] >= 2


def test_sarif(tb):
    r = svcheck(tb, "-f", "tb.f", "--rules", "r.yaml", "--format", "sarif")
    doc = json.loads(r.stdout)
    assert doc["version"] == "2.1.0"
    (run,) = doc["runs"]
    assert [x["id"] for x in run["tool"]["driver"]["rules"]] == ["ACME001", "ACME002"]
    res = run["results"][0]
    assert (res["ruleId"], res["level"]) == ("ACME001", "warning")
    loc = res["locations"][0]["physicalLocation"]
    assert loc["artifactLocation"]["uri"] == "agent/apb_driver.svh"
    assert (loc["region"]["startLine"], loc["region"]["startColumn"]) == (5, 5)


def test_a_broken_compilation_runs_only_syntax_rules(tb):
    (tb / "bad.sv").write_text("package bad; int x = undefined_thing; endpackage\n")
    r = svcheck(tb, "-f", "tb.f", "bad.sv", "--rules", "r.yaml")
    assert r.returncode == 1
    assert r.stdout.splitlines() == [
        "svcheck: info: rule ACME001 (objection-in-agent) not run: it needs the compilation, "
        "which has 1 error [SVCHK231]",
        "bad.sv:1:9: warning: package 'bad' does not end in _pkg [ACME002]",
    ]
    assert "use of undeclared identifier 'undefined_thing'" in r.stderr
    assert r.stderr.splitlines()[-1] == (
        "svcheck: 0 errors, 1 warning; the compilation has 1 error")


def test_a_file_with_syntax_errors_is_not_linted(tb):
    (tb / "bad.sv").write_text("package bad_x; int x = ; endpackage\n")
    r = svcheck(tb, "-f", "tb.f", "bad.sv", "--rules", "r.yaml", "--format", "json")
    doc = json.loads(r.stdout)
    assert doc["compilation"]["unparsed"] == ["bad.sv"]
    assert [f["code"] for f in doc["findings"]] == ["SVCHK231"]


def test_single_unit_lints_each_file_that_parses(tb):
    # --single-unit makes one syntax tree of every file, uvm_pkg's first:
    # the user's files are still linted, and a syntax error hides only its
    # own file's findings.
    (tb / "bad.sv").write_text("package bad_x; int x = ; endpackage\n")
    (tb / "other.sv").write_text("package other; endpackage\n")
    r = svcheck(tb, "--single-unit", "-f", "tb.f", "bad.sv", "other.sv", "--rules", "r.yaml",
                "--format", "json")
    doc = json.loads(r.stdout)
    assert doc["compilation"]["unparsed"] == ["bad.sv"]
    assert [(f["code"], f["file"]) for f in doc["findings"]] == [
        ("SVCHK231", None), ("ACME002", "other.sv")]


def test_applies_to_with_no_classifiers_is_not_run(tb):
    rules = RULES[:RULES.index("  contexts:")] + RULES[RULES.index("  rules:"):]
    (tb / "r.yaml").write_text(rules)
    r = svcheck(tb, "-f", "tb.f", "--rules", "r.yaml")
    assert r.stdout.splitlines() == [
        "svcheck: info: rule ACME001 (objection-in-agent) not run: no unit has a context on "
        "axis 'unit' [SVCHK231]"]
    assert r.returncode == 0


def test_contexts_text(tb):
    r = svcheck(tb, "-f", "tb.f", "--rules", "r.yaml", "--contexts")
    assert r.returncode == 0, r.stderr
    assert r.stdout.splitlines() == [
        "unit apb_agent_pkg: agent",
        "  Package apb_agent_pkg  agent/apb_agent_pkg.sv:1",
        "    unit agent: Content (r.yaml:4) on Package apb_agent_pkg",
        "unit env_pkg: env",
        "  Package env_pkg  env_pkg.sv:1",
        "    unit env: Content (r.yaml:8) on Package env_pkg",
        "dependencies:",
        "  env_pkg -> apb_agent_pkg (Import)",
    ]


def test_contexts_json(tb):
    r = svcheck(tb, "-f", "tb.f", "--rules", "r.yaml", "--contexts", "--format", "json")
    doc = json.loads(r.stdout)
    assert [(u["name"], u["labels"]) for u in doc["units"]] == [
        ("apb_agent_pkg", ["agent"]), ("env_pkg", ["env"])]
    assert doc["units"][0]["carriers"] == [
        {"kind": "Package", "name": "apb_agent_pkg", "file": "agent/apb_agent_pkg.sv",
         "line": 1}]
    assert doc["deps"] == [{"from": "env_pkg", "to": "apb_agent_pkg", "kind": "Import"}]
    assert doc["notes"] == []


def test_library_dir_is_not_linted(tb):
    (tb / "r.yaml").write_text(RULES.replace("endsWith(\"_pkg\")", "endsWith(\"_none\")"))
    r = svcheck(tb, "-f", "tb.f", "--rules", "r.yaml", "--library-dir", "agent")
    assert [l.split(":")[0] for l in r.stdout.splitlines()] == ["env_pkg.sv"]
    # The agent is library code now, so the classifier says it is no agent.
    assert "apb_driver.svh" not in r.stdout


def test_bad_rule_file_checks_nothing(tb):
    (tb / "r.yaml").write_text(RULES.replace("match: { kind: Call }", "match: { kind: Cal }"))
    r = svcheck(tb, "-f", "tb.f", "--rules", "r.yaml")
    assert r.returncode == 2
    assert r.stdout == ""
    assert "[SVCHK213]" in r.stderr
    assert r.stderr.splitlines()[-1] == (
        "svcheck: the rule files have errors; nothing was checked")


@pytest.mark.parametrize("args, message", [
    (["--format", "xml"], "svcheck: unknown --format 'xml' (text, json, sarif)"),
    (["--semantic", "deep"], "svcheck: unknown --semantic 'deep' (model, slang, none)"),
    (["--contexts"], "svcheck: --contexts needs --rules: the classifiers are in the rule files"),
])
def test_usage_errors(tb, args, message):
    r = svcheck(tb, "-f", "tb.f", *args)
    assert (r.returncode, r.stderr.strip()) == (2, message)


def test_missing_rule_file(tb):
    r = svcheck(tb, "-f", "tb.f", "--rules", "nope.yaml")
    assert (r.returncode, r.stderr.strip()) == (2, "svcheck: cannot read rule file 'nope.yaml'")
