"""The model has what the consensus pack needs (svcheck-plan P2.2;
sv-uvm-rules-assessment §5.1, 36 rules).

For each pack rule, its core predicate written as a CEL expression over the
curated model, run on a snippet that holds a case it must find. A line marked
`// @F7` is where rule F7's probe must find something, and only there (a
line can carry several marks). These are probes, not the rules: P2.5 writes
the rules, with options, messages and near-misses, in `uvm.yaml` and the
pack. What is checked here is that each kind, field and function a rule's
predicate reads exists and means what the rule needs.

The probes are grouped by the pack's batches: K, T, S, L first, then F, P,
G, A (the families that use contexts).
"""
import os
import re
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tests", "engine"))

from hooks import EngineHooks  # noqa: E402

UVM = os.path.join(ROOT, "packages", "uvm-core", "src")

pytestmark = pytest.mark.skipif(not os.path.isfile(os.path.join(UVM, "uvm_pkg.sv")),
                                reason="UVM is not in packages/ (ivpm update)")

SRC = r"""
import uvm_pkg::*;
`include "uvm_macros.svh"
interface bus_if(input logic clk);
  logic [7:0] data;
  clocking cb @(posedge clk); output data; endclocking
endinterface

class item extends uvm_sequence_item;
  `uvm_object_utils(item)
  rand int x;
  function new(string name = "item"); super.new(name); endfunction   // @K1
  function void do_copy(uvm_object rhs); super.do_copy(rhs); endfunction
  function string convert2string(); return $sformatf("%0d", x); endfunction
endclass

class unreg extends uvm_object;                                       // @F1 @T3
  function new(string name = ""); super.new(name); endfunction
endclass

class txn extends uvm_transaction;                                    // @F1 @T1 @T3
  function new(string name = ""); super.new(name); endfunction
endclass

class cfg extends uvm_object;
  `uvm_object_utils(cfg)
  virtual bus_if vif;
  function new(string name = ""); super.new(name); endfunction
endclass

class seq extends uvm_sequence #(item);
  `uvm_object_utils(seq)
  function new(string name = ""); super.new(name); endfunction
  task pre_body(); endtask                                            // @S6
  task body();
    item it = item::type_id::create("it");
    start_item(it);
    void'(it.randomize());                                            // @S2
    finish_item(it);
  endtask
endclass

class drv extends uvm_driver #(item);
  `uvm_component_utils(drv)
  cfg c;
  seq s;
  function new(string name, uvm_component parent = null);            // @K2
    super.new(name, parent);
  endfunction
  function void build_phase(uvm_phase phase);                         // @P4
    super.build_phase(phase);
    void'(uvm_config_db#(cfg)::get(this, "", "cfg", c));
  endfunction
  task run_phase(uvm_phase phase);
    item it;
    phase.raise_objection(this);                                      // @P10
    it = item::type_id::create("itm");                                // @F7
    s.start_item(it);                                                 // @S9
    c.vif.cb.data <= 1;
    #0;                                                               // @L3
    phase.drop_objection(this, "done");
  endtask
endclass

class mon extends uvm_monitor;
  `uvm_component_utils(mon)
  cfg c;
  function new(string name, uvm_component parent); super.new(name, parent); endfunction
  task run_phase(uvm_phase phase);
    c.vif.data = 0;                                                   // @A4
    @(posedge c.vif.clk);
  endtask
endclass

class env extends uvm_env;
  `uvm_component_utils(env)
  drv d;
  mon m;
  uvm_analysis_port #(item) ap;
  function new(string name, uvm_component parent); super.new(name, parent); endfunction
  function void build_phase(uvm_phase phase);
    item i = new("i");                                                // @F5
    super.connect_phase(phase);                                       // @P3
    d = drv::type_id::create("d", this);
    m = mon::type_id::create("m", null);                              // @F9
    ap = new("ap", this);
    m.c = new();                                                      // @F5
    m.c.print();                                                      // @T8
    uvm_config_db#(int)::set(this, "*", "count", 1);                  // @G3
    uvm_config_db#(int)::get(this, "", "count", i.x);                 // @G2
  endfunction
  function void connect_phase(uvm_phase phase);
    super.connect_phase(phase);
    d.seq_item_port.connect(null);
  endfunction
  task run_phase(uvm_phase phase);
    m = mon::type_id::create("m2", this);                             // @F7 @F16
    ap.connect(null);                                                 // @P8
    #5;                                                               // @A7
    $display("hello");                                                // @A12
    $finish;                                                          // @P14
  endtask
endclass

class env2 extends env;
  `uvm_component_utils(env2)
  function new(string name, uvm_component parent); super.new(name, parent); endfunction
  task run_phase(uvm_phase phase);                                    // @P1
  endtask
endclass

class base_test extends uvm_test;
  `uvm_component_utils(base_test)
  function new(string name, uvm_component parent); super.new(name, parent); endfunction
  function void build_phase(uvm_phase phase);
    uvm_root r = uvm_top;                                             // @F13
    uvm_factory::get().set_type_override_by_name("drv", "drv");       // @F11
    uvm_config_db#(uvm_object_wrapper)::set(this, "env.sqr.run_phase",
                                            "default_sequence",       // @S5
                                            seq::get_type());
    uvm_report_info("ID", "msg");                                     // @K14
  endfunction
  task run_phase(uvm_phase phase);
    for (int i = 0; i < 2; i++) fork
      $display("%0d", i);                                             // @L12 @A12
    join_none
    fork #1; #2; join_any                                             // @A7
    disable fork;                                                     // @L12
    begin : blk
      disable blk;                                                    // @L12
    end
    $random;                                                          // @L7
  endtask
endclass

module tb;
  logic clk;
  bus_if b(clk);
  if (1) begin : g
    initial begin
      uvm_config_db#(virtual bus_if)::set(null, "*", "vif", b);       // @G6
      run_test("base_test");                                          // @P13
      $display("after");                                              // @P14
    end
  end
endmodule
"""

# A user class, a user subroutine, a user call: library code has no body.
UCLS = 'descendants(node, Class).filter(c, !c.inLibrary)'
CALLS = 'descendants(node, Call).filter(k, !in_library(k))'
LN = '.map(x, x.location.lineno)'
DERIVES = 'derives_from(%s, "uvm_pkg::%s")'
CLASS = 'enclosing(%s, Class)'
METHOD = 'enclosing(%s, Subroutine)'
PHASES = ('["build_phase", "connect_phase", "end_of_elaboration_phase", '
          '"start_of_simulation_phase", "extract_phase", "check_phase", '
          '"report_phase", "final_phase"]')

PROBES = {
    # ---- K: construction
    # K1: an object's constructor is `new(string name = "")`.
    "K1": UCLS + '.filter(c, !' + DERIVES % ("c", "uvm_component") + ' && '
          + DERIVES % ("c", "uvm_object") + ')'
          '.map(c, c.members.filter(m, m.name == "new")).filter(l, l.size() == 1)'
          '.map(l, l[0]).filter(s, s.args.size() != 1 || !s.args[0].type.isString'
          ' || s.args[0].direction != ArgDirection.In || s.args[0].init == null'
          ' || const_string(s.args[0].init) != optional.of(""))' + LN,
    # K2: a component's constructor is `(string name, uvm_component parent)`, no defaults.
    "K2": UCLS + '.filter(c, ' + DERIVES % ("c", "uvm_component") + ')'
          '.map(c, c.members.filter(m, m.name == "new")).filter(l, l.size() == 1)'
          '.map(l, l[0]).filter(s, s.args.exists(a, a.init != null))' + LN,
    # K4: `super.new` is the first statement (a positive probe: it is, everywhere).
    "K4": UCLS + '.map(c, c.members.filter(m, m.name == "new")).filter(l, l.size() == 1)'
          '.map(l, l[0]).filter(s, s.body.stmts.size() == 0'
          ' || !(s.body.stmts[0].isa(ExprStmt) && s.body.stmts[0].expr.isa(NewClass)'
          ' && s.body.stmts[0].expr.isSuper))' + LN,
    # K14: deprecated or removed API, by the called subroutine's qualified name.
    "K14": CALLS + '.filter(k, k.subroutine != null && k.subroutine.qname in '
           '["uvm_pkg::uvm_report_object::uvm_report_info"])' + LN,
    # ---- T: transactions
    "T1": UCLS + '.filter(c, ' + DERIVES % ("c", "uvm_transaction") + ' && !'
          + DERIVES % ("c", "uvm_sequence_item") + ')' + LN,
    # T3: an object (not a component or sequence) overrides convert2string and do_copy.
    "T3": UCLS + '.filter(c, ' + DERIVES % ("c", "uvm_object") + ' && !'
          + DERIVES % ("c", "uvm_component") + ' && !' + DERIVES % ("c", "uvm_sequence_base")
          + ' && c.name != "cfg" && ["convert2string", "do_copy"].exists(n, '
          '!c.members.exists(m, m.name == n)))' + LN,
    # T8: print()/sprint() on a uvm_object.
    "T8": CALLS + '.filter(k, k.name in ["print", "sprint"] && k.receiver != null && '
          + DERIVES % ("k.receiver.type", "uvm_object") + ')' + LN,
    # ---- S: sequences
    # S2: randomize() discarded (void') or in an assertion.
    "S2": CALLS + '.filter(k, k.name == "randomize" && (parent(k).isa(ExprStmt) && '
          'parent(k).isVoidCast || (parent(k).isa(Conversion) && parent(k).type.name == "void")))' + LN,
    # S5: config_db set of "default_sequence".
    "S5": CALLS + '.filter(k, k.name == "set" && k.classType != null && '
          'k.classType.classDecl.qname == "uvm_pkg::uvm_config_db" && '
          'const_string(k.args[2]) == optional.of("default_sequence"))'
          '.map(k, k.args[2].location.lineno)',
    # S6: a sequence overrides pre_body/post_body.
    "S6": 'descendants(node, Subroutine).filter(s, !s.inLibrary && s.overrides != null && '
          's.name in ["pre_body", "post_body"] && '
          + DERIVES % (CLASS % "s", "uvm_sequence_base") + ')' + LN,
    # S9: start_item/finish_item outside a sequence.
    "S9": CALLS + '.filter(k, k.name in ["start_item", "finish_item"] && !'
          + DERIVES % (CLASS % "k", "uvm_sequence_base") + ')' + LN,
    # ---- L: language
    # L3: `#0`.
    "L3": 'descendants(node, Timed).filter(t, t.kind == TimingKind.Delay && '
          '(const_int(t.exprs[0]) == optional.of(0) || '
          '(t.exprs[0].constKind == ConstKind.Real && t.exprs[0].constReal == 0.0)))' + LN,
    # L7: forbidden system functions.
    "L7": CALLS + '.filter(k, k.isSystem && k.name in ["$random", "$psprintf", "$srandom"])' + LN,
    # L12: a loop variable in a join_none, `disable fork` right after a join_any
    # (not in an isolating fork), `disable` by label.
    "L12": 'descendants(node, NamedValue).filter(v, v.decl != null && '
           'ancestors(v).exists(b, b.isa(Block) && b.kind == BlockKind.JoinNone && '
           'ancestors(b).exists(l, l.isa(Loop) && l.vars.exists(lv, lv == v.decl))))' + LN
           + ' + descendants(node, Disable).filter(d, d.isFork && before(d).size() > 0 && '
           'before(d)[0].isa(Block) && before(d)[0].kind == BlockKind.JoinAny)' + LN
           + ' + descendants(node, Disable).filter(d, !d.isFork)' + LN,
    # ---- F: factory
    # F1: a UVM class, not abstract, with no `type_id` of its own.
    "F1": UCLS + '.filter(c, !c.isAbstract && ' + DERIVES % ("c", "uvm_object") + ' && '
          '!c.members.exists(m, m.name == "type_id"))' + LN,
    # F2, F3: the registry's flavour and type argument (positive: all agree).
    "F2": UCLS + '.filter(c, c.members.exists(m, m.name == "type_id"))'
          '.map(c, [c, c.members.filter(m, m.name == "type_id")[0].type])'
          '.filter(p, ' + DERIVES % ("p[0]", "uvm_component") + ' != '
          '(p[1].classDecl.name == "uvm_component_registry"))' '.map(p, p[0].location.lineno)',
    "F3": UCLS + '.filter(c, c.members.exists(m, m.name == "type_id"))'
          '.filter(c, c.members.filter(m, m.name == "type_id")[0].type.args[0].classDecl != c)' + LN,
    # F5: new() of a registered UVM object; not super.new, not a TLM port.
    "F5": 'descendants(node, NewClass).filter(n, !in_library(n) && !n.isSuper && '
          + DERIVES % ("n.type", "uvm_object") + ' && !' + DERIVES % ("n.type", "uvm_port_base")
          + ' && n.type.classDecl.members.exists(m, m.name == "type_id") && '
          'n.type.classDecl != ' + CLASS % "n" + ')' + LN,
    # F7: `x = T::type_id::create("y", ...)`: the name is not the variable's.
    "F7": 'descendants(node, Assign).filter(a, !in_library(a) && a.rhs.isa(Call) && '
          'a.rhs.name == "create" && a.lhs.isa(NamedValue) && '
          'const_string(a.rhs.args[0]) != optional.of(a.lhs.name))' + LN,
    # F9: a component's create() parent is not `this`.
    "F9": CALLS + '.filter(k, k.name == "create" && ' + DERIVES % ("k.type", "uvm_component")
          + ' && !(k.args[1].isa(Conversion) && k.args[1].operand.isa(NamedValue) && '
          'k.args[1].operand.name == "this"))' + LN,
    "F11": CALLS + '.filter(k, k.name in ["set_type_override_by_name", '
           '"set_inst_override_by_name"])' + LN,
    # F13: a uvm_pkg global variable read (the probe's `uvm_top`; the rule's `factory`).
    "F13": 'descendants(node, NamedValue).filter(v, !in_library(v) && v.decl != null && '
           'v.decl.qname == "uvm_pkg::uvm_top")' + LN,
    # F16: a component created outside build_phase.
    "F16": CALLS + '.filter(k, k.name == "create" && ' + DERIVES % ("k.type", "uvm_component")
           + ' && ' + METHOD % "k" + ' != null && ' + METHOD % "k" + '.name != "build_phase")' + LN,
    # ---- P: phasing
    # P1: a phase override of user code that does not call super (spike UVM030's shape).
    "P1": 'descendants(node, Subroutine).filter(s, !s.inLibrary && s.overrides != null && '
          's.name.endsWith("_phase") && !s.overrides.inLibrary && '
          '!descendants(s, Call).exists(k, k.isSuper && k.name == s.name))' + LN,
    # P3: super.X() in a method that is not X.
    "P3": CALLS + '.filter(k, k.isSuper && ' + METHOD % "k" + ' != null && '
          'k.name != ' + METHOD % "k" + '.name)' + LN,
    # P4: a driver overrides a phase other than run_phase.
    "P4": 'descendants(node, Subroutine).filter(s, !s.inLibrary && s.overrides != null && '
          's.name in ' + PHASES + ' && ' + DERIVES % (CLASS % "s", "uvm_driver") + ')' + LN,
    # P8: a port's connect() outside connect_phase.
    "P8": CALLS + '.filter(k, k.name == "connect" && k.receiver != null && '
          + DERIVES % ("k.receiver.type", "uvm_port_base") + ' && '
          + METHOD % "k" + '.name != "connect_phase")' + LN,
    # P10: raise/drop_objection without a description.
    "P10": CALLS + '.filter(k, k.name in ["raise_objection", "drop_objection"] && '
           'k.args.size() > 1 && k.args[1].isDefault)' + LN,
    # P13: run_test() with an argument.
    "P13": CALLS + '.filter(k, k.name == "run_test" && k.args.size() > 0 && '
           '!k.args[0].isDefault)' + LN,
    # P14: $finish in a class; statements after run_test().
    "P14": CALLS + '.filter(k, k.isSystem && k.name in ["$finish", "$stop"] && '
           + CLASS % "k" + ' != null)' + LN
           + ' + ' + CALLS + '.filter(k, k.name == "run_test").map(k, after(k))'
           '.filter(l, l.size() > 0).map(l, l[0].location.lineno)',
    # ---- G: configuration
    # G2: config_db get() result discarded, without a void' cast (whether a
    # cast excuses it is the rule's choice, P2.5).
    "G2": CALLS + '.filter(k, k.name == "get" && k.classType != null && '
          'k.classType.classDecl.qname == "uvm_pkg::uvm_config_db" && '
          'parent(k).isa(ExprStmt) && !parent(k).isVoidCast)' + LN,
    # G3: a wildcard in the field name.
    "G3": CALLS + '.filter(k, k.name in ["set", "get"] && k.classType != null && '
          'k.classType.classDecl.qname == "uvm_pkg::uvm_config_db" && '
          'const_string(k.args[1]).orValue("").contains("*") && '
          + CLASS % "k" + ' != null)' + LN,
    # G6: a vif set from a module, through a generate block (S-1).
    "G6": CALLS + '.filter(k, k.name == "set" && k.classType != null && '
          'k.classType.args.size() > 0 && k.classType.args[0].isVirtualInterface && '
          'enclosing(k, Definition) != null && enclosing(k, GenBlock) != null)' + LN,
    # ---- A: components
    # A4: a monitor writes through a virtual interface, however reached.
    "A4": 'descendants(node, Assign).filter(a, !in_library(a) && '
          + DERIVES % (CLASS % "a", "uvm_monitor") + ' && '
          'access_path(a.lhs).exists(p, p.type.isVirtualInterface))' + LN,
    # A7: a component other than a driver or monitor waits.
    "A7": 'descendants(node, Timed).filter(t, ' + CLASS % "t" + ' != null && '
          + DERIVES % (CLASS % "t", "uvm_component") + ' && !'
          + DERIVES % (CLASS % "t", "uvm_driver") + ' && !'
          + DERIVES % (CLASS % "t", "uvm_monitor") + ' && t.kind == TimingKind.Delay)' + LN,
    # A12: $display/$write in a UVM class.
    "A12": CALLS + '.filter(k, k.isSystem && k.name in ["$display", "$write"] && '
           + CLASS % "k" + ' != null && ' + DERIVES % (CLASS % "k", "uvm_component") + ')' + LN,
}

# Rules of the 36 whose probe only checks that the fields exist and agree:
# their snippet has nothing for them to find.
POSITIVE = {"K4", "F2", "F3"}


def _marks():
    out = {}
    for i, line in enumerate(SRC.split("\n"), start=1):
        for m in re.findall(r"@([A-Z][0-9]+)", line):
            out.setdefault(m, []).append(i)
    return out


MARKS = _marks()


@pytest.fixture(scope="module")
def model():
    h = EngineHooks(semantic="model", uvm=UVM)
    yield h
    h.close()


def test_every_pack_rule_has_a_probe():
    pack = ("F1 F2 F3 F5 F7 F9 F11 F13 F16 K1 K2 K4 K14 T1 T3 T8 S2 S5 S6 S9 "
            "P1 P3 P4 P8 P10 P13 P14 G2 G3 G6 A4 A7 A12 L3 L7 L12").split()
    assert len(pack) == 36
    assert sorted(PROBES) == sorted(pack)
    assert sorted(MARKS) == sorted(set(pack) - POSITIVE)


@pytest.mark.parametrize("rule", sorted(PROBES))
def test_probe(model, rule):
    r = model.cel_eval_node(PROBES[rule], SRC)
    assert r["ok"], r
    res = r["result"]
    assert res["type"] == "list", res
    lines = sorted(set(x["value"] for x in res["value"]))
    assert lines == MARKS.get(rule, []), (rule, lines)
