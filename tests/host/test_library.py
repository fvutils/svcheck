"""The SV function library over the curated model (svcheck-plan P1.4;
src/host/SvLib.cpp): classes, symbols, types, constants, macros, carriers.
"""
import os
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tests", "engine"))

from hooks import EngineHooks  # noqa: E402

UVM = os.path.join(ROOT, "packages", "uvm-core", "src")
needs_uvm = pytest.mark.skipif(not os.path.isfile(os.path.join(UVM, "uvm_pkg.sv")),
                               reason="UVM is not in packages/ (ivpm update)")


@pytest.fixture(scope="module")
def model():
    h = EngineHooks(semantic="model", uvm=UVM)
    yield h
    h.close()


@pytest.fixture(scope="module")
def syntax():
    h = EngineHooks()
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
    if v["type"] == "optional_type":
        return None if v.get("value") is None else _plain(v["value"])
    return v["value"]


# ------------------------------------------------------------------ classes --

CLASSES = ("package p;\n"
           "  class a; int x; int y; virtual function void f(); endfunction endclass\n"
           "  class b extends a; int y; function void f(); endfunction endclass\n"
           "endpackage\n")
B = 'node.packages.filter(p, p.name == "p")[0].items[1]'


def test_member_finds_own_then_inherited(model):
    assert _eval(model, CLASSES, 'member(%s, "y").qname' % B) == "p::b::y"
    assert _eval(model, CLASSES, 'member(%s, "x").qname' % B) == "p::a::x"
    assert _eval(model, CLASSES, 'member(%s, "z") == null' % B) is True
    assert _eval(model, CLASSES, 'member(%s.members[0], "y") == null' % B) is True


def test_members_hide_by_name(model):
    assert _eval(model, CLASSES, 'members(%s).map(m, m.qname)' % B) == [
        "p::b::y", "p::b::f", "p::a::x"]


def test_owner(model):
    assert _eval(model, CLASSES, 'owner(%s.members[1]).qname' % B) == "p::b"
    assert _eval(model, CLASSES, 'owner(%s).name' % B) == "p"
    assert _eval(model, CLASSES, 'owner(node.packages[0]) == null') is True


# ------------------------------------------------ symbols, types, constants --

BODY = ("interface bus_if; logic clk; endinterface\n"
        "class c;\n"
        "  typedef enum {RED, GREEN = 5} color_e;\n"
        "  localparam int W = 8;\n"
        "  localparam string S = \"hi\";\n"
        "  int n; logic [W-1:0] v; string s; virtual bus_if vif;\n"
        "  function int g(); return 1; endfunction\n"
        "  function void f();\n"
        "    c o = new;\n"
        "    n = W * 2 + GREEN;\n"
        "    s = \"lit\";\n"
        "    n = g();\n"
        "    s = S;\n"
        "    n = n + 1;\n"
        "  endfunction\n"
        "endclass\n")
C = 'node.items.filter(i, i.name == "c")[0]'
F = 'member(%s, "f")' % C


def _stmt(i):
    return '%s.body.stmts[%d]' % (F, i)


def test_resolves_to(model):
    assert _eval(model, BODY, 'resolves_to(%s.expr.lhs).qname' % _stmt(1)) == "c::n"
    assert _eval(model, BODY, 'resolves_to(%s.expr.rhs).qname' % _stmt(3)) == "c::g"
    assert _eval(model, BODY, 'resolves_to(%s.var.init).qname' % _stmt(0)) == "c"
    assert _eval(model, BODY, 'resolves_to(%s.expr) == null' % _stmt(1)) is True


def test_type_of(model):
    def t(name, field):
        return _eval(model, BODY, 'type_of(member(%s, "%s")).%s' % (C, name, field))
    assert (t("n", "name"), t("n", "isIntegral"), t("n", "isFourState"), t("n", "bitWidth")) == (
        "int", True, False, 32)
    assert (t("v", "isFourState"), t("v", "bitWidth")) == (True, 8)
    assert (t("s", "isString"), t("s", "bitWidth")) == (True, 0)
    assert (t("vif", "isVirtualInterface"), t("vif", "ifaceDecl.name")) == (True, "bus_if")
    assert t("g", "name") == "int"                                   # a return type
    assert _eval(model, BODY, 'type_of(%s.var.init).classDecl.name' % _stmt(0)) == "c"
    # One TypeRef per type: two `int` declarations share it.
    assert _eval(model, BODY, 'type_of(member(%s, "n")) == type_of(member(%s, "g"))'
                 % (C, C)) is True


def test_const_int_and_string(model):
    assert _eval(model, BODY, 'const_int(%s.expr.rhs)' % _stmt(1)) == 21
    assert _eval(model, BODY, 'const_string(%s.expr.rhs)' % _stmt(2)) == "lit"
    assert _eval(model, BODY, 'const_string(%s.expr.rhs)' % _stmt(4)) == "hi"
    assert _eval(model, BODY, 'const_int(%s.expr.rhs).hasValue()' % _stmt(3)) is False
    assert _eval(model, BODY, 'const_int(%s.expr.rhs).hasValue()' % _stmt(5)) is False
    assert _eval(model, BODY, 'const_string(%s.expr.rhs).hasValue()' % _stmt(1)) is False


def test_in_library_reaches_through_expressions(model):
    assert _eval(model, BODY, 'in_library(%s.expr)' % _stmt(1)) is False
    assert _eval(model, BODY, 'in_library(%s)' % C) is False


# ------------------------------------------------------------ uvm, macros --

UVM_SRC = ("import uvm_pkg::*;\n"
           "`include \"uvm_macros.svh\"\n"
           "interface bus_if; logic clk; endinterface\n"
           "class drv extends uvm_component;\n"
           "  `uvm_component_utils(drv)\n"
           "  virtual bus_if vif;\n"
           "  function new(string name, uvm_component parent); super.new(name, parent); endfunction\n"
           "  function void build_phase(uvm_phase phase);\n"
           "    void'(uvm_config_db#(virtual bus_if)::get(this, \"\", \"vif\", vif));\n"
           "  endfunction\n"
           "endclass\n")
DRV = 'node.items.filter(i, i.name == "drv")[0]'


@needs_uvm
def test_macro_of_names_the_macro_that_declared(model):
    assert _eval(model, UVM_SRC, 'macro_of(member(%s, "type_id"))' % DRV) == (
        "uvm_component_utils")
    assert _eval(model, UVM_SRC, 'member(%s, "type_id").macro' % DRV) == "uvm_component_utils"
    assert _eval(model, UVM_SRC, 'macro_of(member(%s, "vif"))' % DRV) == ""


@needs_uvm
def test_a_call_knows_its_class_specialisation(model):
    call = 'calls_in(member(%s, "build_phase"))[0]' % DRV
    assert _eval(model, UVM_SRC, call + '.name') == "get"
    assert _eval(model, UVM_SRC, call + '.classType.classDecl.qname') == "uvm_pkg::uvm_config_db"
    assert _eval(model, UVM_SRC, call + '.classType.args.map(a, a.ifaceDecl.name)') == ["bus_if"]
    assert _eval(model, UVM_SRC, 'in_library(%s.subroutine)' % call) is True
    assert _eval(model, UVM_SRC, 'owner(%s.subroutine).qname' % call) == "uvm_pkg::uvm_config_db"


@needs_uvm
def test_classes_in_a_carrier(model):
    src = ("package p; class a; endclass class b; class n; endclass endclass endpackage\n")
    assert _eval(model, src, 'classes(node.packages.filter(p, p.name == "p")[0])'
                             '.map(c, c.name)') == ["a", "b", "n"]


def test_macro_of_a_syntax_node(syntax):
    src = ("`define DECL(n) int n;\n"
           "module m;\n"
           "  `DECL(x)\n"
           "  int y;\n"
           "endmodule\n")
    items = 'node.members[0].members'
    assert _eval(syntax, src, '%s.map(i, macro_of(i))' % items) == ["DECL", ""]


def test_the_model_functions_are_the_models(syntax):
    # Registered only with the model installed: their signatures name its classes.
    names = {f["function"] for f in syntax.cel_functions()["functions"]}
    assert "macro_of" in names and "derives_from" in names
    assert not names & {"member", "type_of", "const_int", "classes", "calls_in"}
