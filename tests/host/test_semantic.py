"""The semantic layers (svcheck-plan P1.3; svcheck-design §3.6, Q-SC5).

Two layers answer the same 4 UVM rules (sv-uvm-rules-assessment §4):

- `model`: the curated model, lowered from slang (src/ast, src/lower);
- `slang`: a thin accessor table over slang's own AST (src/host/SlangSchema.cpp).

Each layer's rule file is in tests/host/rules/, and both run on the same
examples (tests/host/rules/examples.yaml), whose `// expect: ID` comments say
where a finding must be. The node hooks compile their source with slang,
against uvm_pkg from packages/uvm-core when it mentions `uvm_`.

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

from hooks import EngineHooks  # noqa: E402

UVM = os.path.join(ROOT, "packages", "uvm-core", "src")
RULES = os.path.join(HERE, "rules")

needs_uvm = pytest.mark.skipif(not os.path.isfile(os.path.join(UVM, "uvm_pkg.sv")),
                               reason="UVM is not in packages/ (ivpm update)")

with open(os.path.join(RULES, "examples.yaml"), encoding="utf-8") as _fp:
    EXAMPLES = yaml.safe_load(_fp)


@pytest.fixture(scope="module", params=["model", "slang"])
def layer(request):
    h = EngineHooks(semantic=request.param, uvm=UVM)
    yield request.param, h
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

def _cases():
    for rid, codes in EXAMPLES.items():
        for i in range(len(codes)):
            yield rid, i


@needs_uvm
@pytest.mark.parametrize("rid,idx", list(_cases()), ids=lambda x: str(x))
def test_both_layers_answer_the_uvm_rules_examples(layer, rid, idx):
    name, hooks = layer
    with open(os.path.join(RULES, "uvm-%s.yaml" % name), encoding="utf-8") as fp:
        rules = fp.read()
    code = EXAMPLES[rid][idx]
    want = [i + 1 for i, line in enumerate(code.splitlines()) if "expect: " + rid in line]
    r = hooks.lint_run("uvm-%s.yaml" % name, rules, code, enabled=[rid])
    assert r["loaded"] and r["diagnostics"] == [] and r["failures"] == [], r
    assert sorted(f["line"] for f in r["findings"] if f["id"] == rid) == want, r["findings"]


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
    assert _eval(model, src, pkg + '.items[0].members[0].vifDefinition.name') == "bus_if"
    tb = 'node.definitions.filter(d, d.name == "tb")[0]'
    assert _eval(model, src, tb + '.imports.map(i, i.name)') == ["agent_pkg"]
    assert _eval(model, src, tb + '.items.map(i, [i.name, i.carrier.name])') == [["d", "tb"]]
    assert _eval(model, src,
                 'node.definitions.filter(d, d.name == "bus_if")[0].kind') == 1  # Interface


def test_units_are_declared_and_empty(model):
    assert _eval(model, "class a; endclass\n", '[node.units.size(), node.deps.size()]') == [0, 0]
