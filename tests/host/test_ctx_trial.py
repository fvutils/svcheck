"""The contexts trial (svcheck-plan P1.6b; rules/trial/, scripts/ctx_trial.py).

- The three trial rules' examples pass, with the content set they use.
- The convention set loads beside them, and a convention alone labels.
- What the trial added to the host: `subclasses(c)`, procedural blocks in the
  model (`Process`, so `run_test()` in an `initial` block is a call), and a
  place for findings on a Unit or a UnitDep.
- Each corpus of tests/corpora/ compiles without errors, when its snapshot is
  in packages/ (tests/corpora/README.md); skipped otherwise.
"""
import json
import os
import subprocess
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tests", "engine"))

from hooks import EngineHooks, check_examples  # noqa: E402
from svcheck import native_path  # noqa: E402

UVM = os.path.join(ROOT, "packages", "uvm-core", "src")
TRIAL = os.path.join(ROOT, "rules", "trial")
CORPORA = os.path.join(ROOT, "tests", "corpora")
pytestmark = pytest.mark.skipif(not os.path.isfile(os.path.join(UVM, "uvm_pkg.sv")),
                                reason="UVM is not in packages/ (ivpm update)")


@pytest.fixture(scope="module")
def model():
    h = EngineHooks(semantic="model", uvm=UVM)
    yield h
    h.close()


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


def test_trial_rule_examples(model):
    path = os.path.join(TRIAL, "contexts.yaml")
    with open(path, encoding="utf-8") as fp:
        results = check_examples(model, path, fp.read())
    assert len(results) == 3


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
    ctx = _lint({"tb.sv": BENCH}, os.path.join(TRIAL, "contexts.yaml"), extra=["--contexts"])
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
                os.path.join(TRIAL, "contexts.yaml"),
                os.path.join(TRIAL, "classify-convention.yaml"), extra=["--contexts"])
    units = _units(ctx)
    # Class names alone: my_base_env, my_base_test, and a class named *_test
    # that is no test at all.
    assert units["my_base_env"] == ["env"]
    assert units["reg_intr_test"] == ["test"]
    assert units["base_agent_pkg"] == ["base-lib", "env", "test"]
    (note,) = ctx["notes"]
    assert "says 'agent', but content says 'base-lib'" in note["message"]


def test_unit_and_unitdep_findings_have_a_place(tmp_path):
    rules = _rule_file(str(tmp_path), """\
sv-rules:
  version: 1
  uses: [%s]
  rules:
    - { id: ACME001, name: u, tier: suspicious, summary: s, message: "${node.name}",
        match: { kind: Unit }, where: 'node.name == "top_env_pkg"' }
    - { id: ACME002, name: d, tier: suspicious, summary: s, message: "${node.to.name}",
        match: { kind: UnitDep }, where: 'node.from.name == "top_test_pkg"' }
""" % os.path.join(TRIAL, "classify-content.yaml"))
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


@pytest.mark.parametrize("corpus", ["uvm-tb-proto", "fw-wb-dma", "opentitan-uart"])
def test_corpus_compiles(corpus):
    if not os.path.isdir(os.path.join(ROOT, "packages", corpus)):
        pytest.skip("packages/%s is not there (tests/corpora/README.md)" % corpus)
    r = subprocess.run([native_path(), "--rules", os.path.join(TRIAL, "contexts.yaml"),
                        "-F", os.path.join(CORPORA, corpus + ".f"), "--format", "json"],
                       capture_output=True, text=True, timeout=300, cwd=ROOT)
    out = json.loads(r.stdout)
    assert out["compilation"]["errors"] == 0, r.stderr
    assert out["compilation"]["unparsed"] == []
