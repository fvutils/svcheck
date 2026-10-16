"""The semantic layer (svcheck-plan P1.3; svcheck-design §3.6): the curated
model, lowered from slang (src/ast, src/lower).

It answers the 4 UVM rules of sv-uvm-rules-assessment §4
(tests/host/rules/uvm-model.yaml). Q-SC5 compared it with a thin accessor
table over slang's own AST on the same rules; P1.8 chose the model and
deleted the table. The examples are in tests/host/rules/examples.yaml, whose
`// expect: ID` comments say where a finding must be: they are given to the
rules as their `examples:` and run by the examples harness
(src/engine/run/Examples.h).
The hooks compile their source with slang, against uvm_pkg from
packages/uvm-core when it mentions `uvm_`, and again from packages/uvm-1.2
(the second UVM configuration, svcheck-plan P1.7).

The model's carrier fields (svcheck-plan R-6, sv-contexts.md §2.2) are
checked on a package that `include`s a file.
"""
import os
import sys

import pytest
import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tests", "engine"))

from hooks import EngineHooks, check_examples  # noqa: E402

UVM = os.path.join(ROOT, "packages", "uvm-core", "src")
UVM12 = os.path.join(ROOT, "packages", "uvm-1.2", "src")
RULES = os.path.join(HERE, "rules")

needs_uvm = pytest.mark.skipif(not os.path.isfile(os.path.join(UVM, "uvm_pkg.sv")),
                               reason="UVM is not in packages/ (ivpm update)")

with open(os.path.join(RULES, "examples.yaml"), encoding="utf-8") as _fp:
    EXAMPLES = yaml.safe_load(_fp)


@pytest.fixture(scope="module", params=[UVM, UVM12], ids=["model", "model-uvm1.2"])
def model_uvm(request):
    uvm = request.param
    if not os.path.isfile(os.path.join(uvm, "uvm_pkg.sv")):
        pytest.skip("%s is not in packages/ (ivpm update)" % uvm)
    h = EngineHooks(semantic="model", uvm=uvm)
    yield h
    h.close()


@pytest.fixture(scope="module")
def model():
    h = EngineHooks(semantic="model", uvm=UVM)
    yield h
    h.close()


def _eval(hooks, source, text):
    r = hooks.cel_eval_node(text, source)
    assert r["ok"], r
    assert r["result"]["type"] != "error", r
    return _plain(r["result"])


def _plain(v):
    if v["type"] == "list":
        return [_plain(x) for x in v["value"]]
    return v["value"]


# ------------------------------------------------------ the exit criterion --

def _rules_with_examples(layer):
    """The layer's rule file, each rule given its examples from examples.yaml."""
    with open(os.path.join(RULES, "uvm-%s.yaml" % layer), encoding="utf-8") as fp:
        doc = yaml.safe_load(fp)
    for r in doc["sv-rules"]["rules"]:
        r["examples"] = [{"code": c} for c in EXAMPLES[r["id"]]]
    return yaml.safe_dump(doc, sort_keys=False)


@needs_uvm
@pytest.mark.parametrize("rid", list(EXAMPLES))
def test_the_model_answers_the_uvm_rules_examples(model_uvm, rid):
    results = check_examples(model_uvm, "uvm-model.yaml", _rules_with_examples("model"), [rid])
    assert len(results) == len(EXAMPLES[rid])


# --------------------------------------------------- the model: structure --

@needs_uvm
def test_library_code_is_declarations_only(model):
    src = ("import uvm_pkg::*;\n"
           "class c extends uvm_component;\n"
           "  function new(string name, uvm_component parent); super.new(name, parent); endfunction\n"
           "  function void build_phase(uvm_phase phase); endfunction\n"
           "endclass\n")
    uvm = 'node.packages.filter(p, p.name == "uvm_pkg")[0]'
    comp = uvm + '.items.filter(i, i.name == "uvm_component")[0]'
    assert _eval(model, src, comp + ".inLibrary") is True
    assert _eval(model, src, comp + '.members.filter(m, m.name == "build_phase")[0].body == null') is True
    assert _eval(model, src, 'node.items[0].inLibrary') is False
    assert _eval(model, src, 'node.items[0].members[1].body != null') is True


@needs_uvm
def test_a_specialisation_is_its_generic_class(model):
    src = ("import uvm_pkg::*;\n"
           "class item extends uvm_sequence_item; endclass\n"
           "class drv extends uvm_driver #(item);\n"
           "  function new(string name, uvm_component parent); super.new(name, parent); endfunction\n"
           "endclass\n")
    assert _eval(model, src, 'node.items[1].base.qname') == "uvm_pkg::uvm_driver"
    assert _eval(model, src, 'node.items[1].supers.map(s, s.name)')[:2] == [
        "uvm_driver", "uvm_component"]
    assert _eval(model, src, 'node.items[1].base.params') == ["REQ", "RSP"]


def test_a_generic_body_comes_from_a_specialisation_the_design_makes(model):
    # S-2 (svcheck-plan P1.7): the default (`V = int`) cannot call vif.ping(),
    # so the body comes from the specialisation the module makes. With none,
    # it is the default's, and the call is not there.
    cls = ("interface bus_if; function void ping(); endfunction endinterface\n"
           "class cfg_p #(type V = int);\n"
           "  V vif;\n"
           "  function void go(); vif.ping(); endfunction\n"
           "endclass\n")
    used = cls + ("module top; bus_if b();\n"
                  "  initial begin cfg_p #(virtual bus_if) c = new(); c.vif = b; end\n"
                  "endmodule\n")
    calls = 'descendants(node, Call).map(c, c.name)'
    assert "ping" in _eval(model, used, calls)
    assert "ping" not in _eval(model, cls, calls)


def test_supers_and_overrides(model):
    src = ("class a; virtual function void f(); endfunction endclass\n"
           "class b extends a; function void f(); endfunction endclass\n"
           "class c extends b; endclass\n")
    assert _eval(model, src, 'node.items[2].supers.map(s, s.name)') == ["b", "a"]
    assert _eval(model, src, 'node.items[1].members[0].overrides.qname') == "a::f"
    assert _eval(model, src, 'node.items[1].members[0].isVirtual') is True


def test_built_in_methods_are_not_members(model):
    src = "class a; rand int x; endclass\n"
    assert _eval(model, src, 'node.items[0].members.map(m, m.name)') == ["x"]
    assert _eval(model, src, 'node.items[0].members[0].isRand') is True


def test_an_uncurated_expression_keeps_its_operands(model):
    # `+` is not curated: an OpaqueExpr, with the call inside it still walked.
    src = ("class a;\n"
           "  function int f(); return 1; endfunction\n"
           "  function void g(); int x; x = f() + 1; endfunction\n"
           "endclass\n")
    g = 'node.items[0].members[1]'
    assert _eval(model, src, 'descendants(%s, OpaqueExpr).map(e, e.slangKind)' % g) == [
        "BinaryOp"]
    assert _eval(model, src, 'descendants(%s, Call).map(c, c.subroutine.qname)' % g) == ["a::f"]


def test_the_syntax_layer_is_still_there(model):
    r = model.lint_run("r.yaml", (
        "sv-rules:\n"
        "  rules:\n"
        "    - id: ACME001\n"
        "      name: r\n"
        "      tier: style\n"
        "      summary: s\n"
        "      message: m\n"
        "      match: { kind: ClassDeclaration }\n"), "class a; endclass\n")
    assert r["loaded"] and r["diagnostics"] == [], r
    assert [(f["kind"], f["line"]) for f in r["findings"]] == [("ClassDeclaration", 1)]


# ------------------------------------------- the model: carriers (R-6) --

def test_carriers_know_their_files_imports_and_vifs(model, tmp_path):
    inc = tmp_path / "agent_drv.svh"
    inc.write_text("class agent_drv;\n  virtual bus_if vif;\nendclass\n")
    src = ("interface bus_if; logic clk; endinterface\n"
           "package agent_pkg;\n"
           '  `include "%s"\n'
           "endpackage\n"
           "module tb;\n"
           "  import agent_pkg::*;\n"
           "  agent_drv d;\n"
           "endmodule\n" % inc.as_posix())
    pkg = 'node.packages.filter(p, p.name == "agent_pkg")[0]'
    # The package's text is its own file and the file included into it.
    assert _eval(model, src, pkg + '.files') == ["snippet.sv", inc.as_posix()]
    # A class in the included file is in the package.
    assert _eval(model, src, pkg + '.items.map(i, [i.name, i.carrier.name])') == [
        ["agent_drv", "agent_pkg"]]
    # A virtual interface knows its interface; the module its imports.
    assert _eval(model, src, pkg + '.items[0].members[0].type.ifaceDecl.name') == "bus_if"
    tb = 'node.definitions.filter(d, d.name == "tb")[0]'
    assert _eval(model, src, tb + '.imports.map(i, i.name)') == ["agent_pkg"]
    assert _eval(model, src, tb + '.items.map(i, [i.name, i.carrier.name])') == [["d", "tb"]]
    assert _eval(model, src,
                 'node.definitions.filter(d, d.name == "bus_if")[0].kind') == 1  # Interface


def test_units_are_declared_and_empty(model):
    assert _eval(model, "class a; endclass\n", '[node.units.size(), node.deps.size()]') == [0, 0]
