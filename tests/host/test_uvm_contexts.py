"""The UVM pack's contexts (rules/uvm.yaml's classifiers; svcheck-plan P1.6b,
P2.4, P2.5), which the contexts trial's rules/trial/ became at P2.5 (e).

- The pack's content classifiers label units, with `--select SVCHK-UVM`.
- The convention set (tests/data/classify-convention.yaml, P2.6's to ship)
  loads beside them; a convention alone labels, but never selects.
- What the trial added to the host: `subclasses(c)`, procedural blocks in the
  model (`Process`, so `run_test()` in an `initial` block is a call), and a
  place for findings on a Unit or a UnitDep.
- The trial's rules, now the pack's SVCHK-UVM037 (CTX001) and 038 (CTX002),
  on the positive corpus. Their examples are test_cli's (`--check-rules`).
"""
import json
import os
import subprocess

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
from svcheck import native_path  # noqa: E402

UVM = os.path.join(ROOT, "packages", "uvm-core", "src")
CONVENTION = os.path.join(ROOT, "tests", "data", "classify-convention.yaml")
# The built-in UVM pack, which brings its classifiers.
PACK = ["--select", "SVCHK-UVM"]
CORPORA = os.path.join(ROOT, "tests", "corpora")
pytestmark = pytest.mark.skipif(not os.path.isfile(os.path.join(UVM, "uvm_pkg.sv")),
                                reason="UVM is not in packages/ (ivpm update)")


def _lint(files, *rules, extra=()):
    """Runs the CLI on `files` ({name: text}, written to a temporary
    directory) with UVM, and returns its JSON."""
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        paths = []
        for name, text in files.items():
            p = os.path.join(d, name)
            with open(p, "w", encoding="utf-8") as fp:
                fp.write(text)
            paths.append(p)
        cmd = [native_path()]
        for r in rules:
            cmd += ["--rules", r]
        cmd += ["+incdir+" + UVM, os.path.join(UVM, "uvm_pkg.sv")] + paths
        cmd += ["--format", "json"] + list(extra)
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        assert r.returncode in (0, 1), r.stderr
        return json.loads(r.stdout)


def _rule_file(d, text):
    p = os.path.join(d, "r.yaml")
    with open(p, "w", encoding="utf-8") as fp:
        fp.write(text)
    return p


BENCH = """\
`include "uvm_macros.svh"
package base_pkg;
  import uvm_pkg::*;
  class my_base_env extends uvm_env;
    function new(string name, uvm_component parent); super.new(name, parent); endfunction
  endclass
  class my_base_test extends uvm_test;
    function new(string name, uvm_component parent); super.new(name, parent); endfunction
  endclass
endpackage
package bus_agent_pkg;
  import uvm_pkg::*;
  import base_pkg::*;
  class bus_agent extends uvm_agent;
    function new(string name, uvm_component parent); super.new(name, parent); endfunction
  endclass
endpackage
package top_env_pkg;
  import uvm_pkg::*;
  import base_pkg::*;
  class top_env extends my_base_env;
    function new(string name, uvm_component parent); super.new(name, parent); endfunction
  endclass
endpackage
package top_test_pkg;
  import uvm_pkg::*;
  import base_pkg::*;
  class top_test extends my_base_test;
    function new(string name, uvm_component parent); super.new(name, parent); endfunction
  endclass
  class reg_intr_test extends uvm_object;
    function new(string name = ""); super.new(name); endfunction
  endclass
endpackage
module hvl_top;
  import uvm_pkg::*;
  initial run_test();
endmodule
"""


def _units(ctx):
    return {u["name"]: sorted(u["labels"]) for u in ctx["units"]}


def test_content_set_labels_a_base_library_as_such():
    ctx = _lint({"tb.sv": BENCH}, extra=PACK + ["--contexts"])
    units = _units(ctx)
    # Another package derives from its classes: a base library, not an env or test.
    assert units["base_pkg"] == ["base-lib"]
    assert units["bus_agent_pkg"] == ["agent"]
    assert units["top_env_pkg"] == ["env"]
    assert units["top_test_pkg"] == ["test"]
    # run_test() in an initial block: Process bodies are lowered.
    assert units["hvl_top"] == ["tb-top"]
    assert ctx["notes"] == []


def test_convention_alone_labels_and_disagreement_is_noted():
    ctx = _lint({"tb.sv": BENCH.replace("package base_pkg;", "package base_agent_pkg;")
                 .replace("import base_pkg::*;", "import base_agent_pkg::*;")},
                CONVENTION, extra=PACK + ["--contexts"])
    units = _units(ctx)
    conv = {u["name"]: sorted(u["convention_labels"]) for u in ctx["units"]}
    # Class names alone: my_base_env, my_base_test, and a class named *_test
    # that is no test at all. Convention never selects (Q-CX4, svcheck-plan
    # P2.4): their labels are kept apart, and give no unit a kind.
    assert (units["my_base_env"], conv["my_base_env"]) == ([], ["env"])
    assert (units["reg_intr_test"], conv["reg_intr_test"]) == ([], ["test"])
    assert (units["base_agent_pkg"], conv["base_agent_pkg"]) == (["base-lib"], ["env", "test"])
    (note,) = ctx["notes"]
    assert "says 'agent', but content says 'base-lib'" in note["message"]
    # The disagreement is the unit's, for a consistency rule.
    (unit,) = [u for u in ctx["units"] if u["name"] == "base_agent_pkg"]
    assert unit["disagreements"] == [note["message"]]


def test_unit_and_unitdep_findings_have_a_place(tmp_path):
    rules = _rule_file(str(tmp_path), """\
sv-rules:
  version: 1
  uses: [uvm.yaml]
  rules:
    - { id: ACME001, name: u, tier: suspicious, summary: s, message: "${node.name}",
        match: { kind: Unit }, where: 'node.name == "top_env_pkg"' }
    - { id: ACME002, name: d, tier: suspicious, summary: s, message: "${node.to.name}",
        match: { kind: UnitDep }, where: 'node.from.name == "top_test_pkg"' }
""")
    out = _lint({"tb.sv": BENCH}, rules)
    found = sorted((f["code"], f["line"], f["message"]) for f in out["findings"])
    assert found == [("ACME001", 18, "top_env_pkg"), ("ACME002", 25, "base_pkg")]


def test_subclasses(tmp_path):
    rules = _rule_file(str(tmp_path), """\
sv-rules:
  version: 1
  rules:
    - id: ACME003
      name: s
      tier: suspicious
      summary: s
      message: "${node.name}: ${subclasses(node).map(s, s.name) == ['top_env']}"
      match: { kind: Class }
      where: '!node.inLibrary && subclasses(node).size() > 0'
""")
    out = _lint({"tb.sv": BENCH}, rules)
    assert sorted(f["message"] for f in out["findings"]) == [
        "my_base_env: true", "my_base_test: false"]


# That each corpus compiles is tests/host/test_corpora.py's (P2.1).


def test_the_positive_corpus_fires():
    """P1.7's positive corpus: one package holds the agent, env and test, and
    the monitor raises an objection (tests/corpora/ctx-positive.f). P2.4: in
    that mixed package, the scoreboard's and a sequence's objections are not
    in an agent; the monitor's are, for being a monitor."""
    r = subprocess.run([native_path()] + PACK + ["-F", os.path.join(CORPORA, "ctx-positive.f"), "--format", "json"],
                       capture_output=True, text=True, timeout=300, cwd=ROOT)
    out = json.loads(r.stdout)
    assert out["compilation"]["errors"] == 0, r.stderr
    found = [f for f in out["findings"] if f["code"] in ("SVCHK-UVM037", "SVCHK-UVM038")]
    assert [(f["code"], os.path.basename(f["file"]), f["line"]) for f in found] == [
        ("SVCHK-UVM038", "tb_pkg.sv", 4), ("SVCHK-UVM037", "tb_pkg.sv", 68),
        ("SVCHK-UVM037", "tb_pkg.sv", 72)]
    assert all("in agent apb_monitor" in f["message"] for f in found[1:])
