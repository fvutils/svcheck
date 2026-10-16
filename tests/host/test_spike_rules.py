"""The spike's 10 rules (svcheck-plan P1.6; rules/spike/): 3 Verible-equivalent,
4 UVM, 3 fw-hdl.

- Every rule's examples pass, on the curated model.
- Every predicate is at most 15 lines (the P1.6 exit criterion): `where`,
  `let` and the rule's own `define`.
- The Verible-equivalent rules find what Verible finds on a small testbench,
  or differ for a reason scripts/verible_equiv.py names (Verible from
  packages/verible; skipped when absent).
"""
import json
import os
import subprocess
import sys

import pytest
import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tests", "engine"))

from hooks import EngineHooks, check_examples  # noqa: E402
from svcheck import native_path  # noqa: E402

UVM = os.path.join(ROOT, "packages", "uvm-core", "src")
SPIKE = os.path.join(ROOT, "rules", "spike")
VERIBLE = os.path.join(ROOT, "packages", "verible", "bin", "verible-verilog-lint")
pytestmark = pytest.mark.skipif(not os.path.isfile(os.path.join(UVM, "uvm_pkg.sv")),
                                reason="UVM is not in packages/ (ivpm update)")


def _rule_files():
    """The spike's rule files: those with rules (uvm-vocab.yaml only has helpers)."""
    out = []
    for f in sorted(os.listdir(SPIKE)):
        if f.endswith(".yaml"):
            with open(os.path.join(SPIKE, f), encoding="utf-8") as fp:
                if yaml.safe_load(fp)["sv-rules"].get("rules"):
                    out.append(f)
    return out


def _rules():
    for f in _rule_files():
        with open(os.path.join(SPIKE, f), encoding="utf-8") as fp:
            for r in yaml.safe_load(fp)["sv-rules"]["rules"]:
                yield f, r


@pytest.fixture(scope="module")
def model():
    h = EngineHooks(semantic="model", uvm=UVM)
    yield h
    h.close()


def test_there_are_ten_rules():
    assert len(list(_rules())) == 10


@pytest.mark.parametrize("name", _rule_files())
def test_examples(model, name):
    path = os.path.join(SPIKE, name)
    with open(path, encoding="utf-8") as fp:
        results = check_examples(model, path, fp.read())
    assert results


def predicate_lines():
    """Rule ID -> the source lines of its `where`, `let` and `define` values,
    as written (a folded `>-` scalar is one value but several lines)."""
    def lines(text, node):
        body = text[node.start_mark.index:node.end_mark.index].splitlines()
        return len([x for x in body if x.strip() and x.strip() not in (">-", ">", "|", "|-")])

    out = {}
    for f in _rule_files():
        with open(os.path.join(SPIKE, f), encoding="utf-8") as fp:
            text = fp.read()
        root = yaml.compose(text)
        top = dict((k.value, v) for k, v in root.value)["sv-rules"]
        for rule in dict((k.value, v) for k, v in top.value)["rules"].value:
            fields = dict((k.value, v) for k, v in rule.value)
            n = lines(text, fields["where"])
            for key in ("let", "define"):
                if key in fields:
                    n += sum(lines(text, v) for _, v in fields[key].value)
            out[fields["id"].value] = n
    return out


@pytest.mark.parametrize("rid", [r["id"] for _, r in _rules()])
def test_predicate_is_at_most_15_lines(rid):
    n = predicate_lines()[rid]
    assert n <= 15, "%s: %d lines" % (rid, n)


# ------------------------------------------------------ Verible equivalence --

PKG = """\
package p;
  `define PICK(s, v) case (s) 0: v = 1; endcase
  `include "helpers.svh"
  function automatic int g(int a); return a; endfunction
  class c;
    function int h(); return 0; endfunction
  endclass
endpackage
"""

HELPERS = """\
function int f(int a);
  return a;
endfunction
"""

TOP = """\
module top(input logic [1:0] s, output logic y);
  always_comb begin
    case (s)
      2'd0: y = 0;
    endcase
    unique0 case (s)
      2'd0: y = 1;
    endcase
    case (s)
      2'd0: y = 0;
      default: y = 1;
    endcase
  end
  initial begin
    int x;
    x = $random;
    `PICK(s, x)
`ifdef NOT_DEFINED
    x = $random;
`endif
  end
endmodule
"""


@pytest.mark.skipif(not os.path.isfile(VERIBLE), reason="Verible is not in packages/")
def test_verible_equivalence(tmp_path):
    (tmp_path / "p.sv").write_text(PKG)
    (tmp_path / "helpers.svh").write_text(HELPERS)
    (tmp_path / "top.sv").write_text(TOP)
    out = tmp_path / "diff.json"
    r = subprocess.run([sys.executable, os.path.join(ROOT, "scripts", "verible_equiv.py"),
                        "--svcheck", native_path(), "--json", str(out), "--",
                        "--single-unit", "+incdir+.", "p.sv", "top.sv"],
                       cwd=tmp_path, capture_output=True, text=True, timeout=120)
    assert r.returncode in (0, 1), r.stderr
    doc = json.loads(out.read_text())
    assert sorted(doc["files"]) == ["helpers.svh", "p.sv", "top.sv"]
    # case-missing-default at top.sv:3; explicit-function-lifetime in the
    # included file; $random at top.sv:16. The macro's case is svcheck's only
    # by expansion, which the rule leaves out.
    assert doc["agree"] == 3
    assert doc["svcheck_only"] == []
    # The `ifdef'd $random: Verible lints the text, svcheck the compilation.
    assert [(f["line"], f["rule"], "ifdef" in f["why"]) for f in doc["verible_only"]] == [
        (19, "invalid-system-task-function", True)]
