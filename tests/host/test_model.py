"""The curated model's statements, arguments and generate blocks
(svcheck-plan P2.2): what the consensus pack's rules read
(tests/host/test_pack_model.py has a probe per rule).
"""
import os
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tests", "engine"))

from hooks import EngineHooks  # noqa: E402

UVM = os.path.join(ROOT, "packages", "uvm-core", "src")


@pytest.fixture(scope="module")
def model():
    h = EngineHooks(semantic="model", uvm=UVM)
    yield h
    h.close()


def _plain(v):
    if v["type"] == "list":
        return [_plain(x) for x in v["value"]]
    if v["type"] == "optional_type":
        return None if v.get("value") is None else _plain(v["value"])
    return v["value"]


def _eval(hooks, source, text):
    r = hooks.cel_eval_node(text, source)
    assert r["ok"], r
    assert r["result"]["type"] != "error", r
    return _plain(r["result"])


# ------------------------------------------------------- generate blocks (S-1) --

GEN = """\
module leaf; initial $display("leaf"); endmodule
module only_in_gen; initial $display("g"); endmodule
module m #(int P = 0) ();
  initial $display("outside");
  if (P) begin : on
    logic x;
    initial x = 1;
    only_in_gen u();
  end else begin
    initial $display("off");
  end
  if (P > 5) begin : never
    initial $display("never");
  end
  for (genvar i = 0; i < 2; i++) begin : loop
    always_comb begin end
    leaf l();
  end
endmodule
module top;
  m #(.P(0)) a();
  m #(.P(1)) b();
  m #(.P(1)) c();
endmodule
"""


def test_generate_blocks_are_lowered_once_each(model):
    # Each block once, from the first instance that elaborates it: `on` from
    # b, though a (P = 0) comes first; the loop body once, not per iteration.
    assert _eval(model, GEN, 'descendants(node, GenBlock).map(g, '
                 '[g.qname, g.isLoop, g.isElaborated, g.items.map(i, i.name)])') == [
        ["m::on", False, True, ["x", ""]],
        ["m::genblk1", False, True, [""]],
        ["m::never", False, False, []],
        ["m::loop", True, True, ["i", ""]]]


def test_processes_in_generate_blocks_have_their_bodies(model):
    assert _eval(model, GEN, 'descendants(node, Process).filter(p, p.carrier.name == "m")'
                 '.map(p, [p.location.lineno, enclosing(p, GenBlock) == null ? "" : '
                 'enclosing(p, GenBlock).name, p.body != null])') == [
        [4, "", True], [7, "on", True], [10, "genblk1", True], [16, "loop", True]]


def test_a_definition_instantiated_only_in_a_generate_block_is_lowered(model):
    assert _eval(model, GEN, 'node.definitions.filter(d, d.name == "only_in_gen")[0]'
                 '.items.map(i, i.location.lineno)') == [2]


# ------------------------------------------------------------- statements --

STMTS = """\
class c;
  event ev, ev2;
  int q[3];
  task t(bit x);
    #0;
    #5ns x = 0;
    @(ev);
    @(posedge x iff q[0]);
    wait (x);
    wait fork;
    wait_order (ev, ev2);
    fork begin end join_any
    disable fork;
    begin : blk
      disable blk;
    end
    fork : fj join_none
    for (int i = 0, j = 1; i < 3; i++) begin end
    foreach (q[k]) begin end
    void'(f());
    f();
  endtask
  function int f(); return 0; endfunction
  function void g(); f(); endfunction
endclass
"""
T = 'node.items[0].members.filter(m, m.name == "t")[0]'


def test_waits_are_timed_statements(model):
    assert _eval(model, STMTS, 'descendants(%s, Timed).map(t, [t.location.lineno, '
                 'string(t.kind), t.exprs.size(), t.body != null])' % T) == [
        [5, "0", 1, False], [6, "0", 1, True], [7, "1", 1, False], [8, "1", 2, False],
        [9, "3", 1, False], [10, "4", 0, False], [11, "5", 2, False]]
    assert _eval(model, STMTS, 'descendants(%s, Timed).filter(t, t.kind == TimingKind.Delay)'
                 '.map(t, [string(t.exprs[0].constKind), t.exprs[0].constInt, '
                 't.exprs[0].constReal])' % T) == [["1", 0, 0.0], ["3", 0, 5.0]]


def test_blocks_carry_their_join_and_label(model):
    assert _eval(model, STMTS, 'descendants(%s, Block).filter(b, b.kind != BlockKind.Sequential '
                 '|| b.label != "").map(b, [b.location.lineno, string(b.kind), b.label])' % T) == [
        [12, "2", ""], [14, "0", "blk"], [17, "3", "fj"]]
    assert _eval(model, STMTS, 'descendants(%s, Disable).map(d, [d.isFork, d.target])' % T) == [
        [True, ""], [False, "blk"]]


def test_loops_declare_their_variables(model):
    assert _eval(model, STMTS, 'descendants(%s, Loop).map(l, l.vars.map(v, '
                 '[v.name, v.init != null]))' % T) == [
        [["i", True], ["j", True]], [["k", False]]]


def test_a_void_cast_statement_is_marked(model):
    assert _eval(model, STMTS, 'descendants(%s, ExprStmt).filter(s, s.expr.isa(Call))'
                 '.map(s, s.isVoidCast)' % T) == [True, False]


def test_a_subroutine_body_is_always_a_block(model):
    assert _eval(model, STMTS, 'node.items[0].members.filter(m, m.isa(Subroutine))'
                 '.map(s, [s.name, s.body.isa(Block), s.body.stmts.size()])') == [
        ["t", True, 15], ["f", True, 1], ["g", True, 1]]


# ------------------------------------------------------------- arguments --

ARGS = """\
class b;
  function new(string name = "b", int n = 1); endfunction
endclass
class d extends b;
  function new(string name);
    super.new(name);
  endfunction
  task t(input int a, output int o, inout int io, ref int r, const ref int cr);
  endtask
  function void f(int a, int b2 = 5, int c2 = 6); endfunction
  function void g(); int x; f(1); f(1, 2); f(.a(1), .c2(3)); x = 0; endfunction
endclass
"""


def test_formal_arguments_have_directions_and_defaults(model):
    assert _eval(model, ARGS, 'node.items[1].members.filter(m, m.name == "t")[0].args'
                 '.map(a, string(a.direction))') == ["1", "2", "3", "4", "5"]
    assert _eval(model, ARGS, 'node.items[0].members[0].args.map(a, '
                 '[a.name, const_string(a.init), const_int(a.init)])') == [
        ["name", "b", None], ["n", None, 1]]


def test_left_out_arguments_are_marked_default(model):
    assert _eval(model, ARGS, 'descendants(node.items[1], Call).map(k, '
                 'k.args.map(a, a.isDefault))') == [
        [False, True, True], [False, False, True], [False, True, False]]


def test_super_new_is_marked(model):
    assert _eval(model, ARGS, 'descendants(node, NewClass).map(n, '
                 '[n.isSuper, n.type.name, n.args.map(a, a.isDefault)])') == [
        [True, "void", [False, True]]]


def test_variables_that_are_not_arguments_have_no_direction(model):
    assert _eval(model, ARGS, 'descendants(node, Variable).filter(v, v.name == "x")'
                 '.map(v, v.direction == ArgDirection.NotArg)') == [True]
