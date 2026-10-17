// SL-16 (lrm-relaxations, svcheck-plan P2.4b): sample() arguments and bins named start and
// stop, like the built-in methods of covergroups and coverpoints (OpenTitan's i2c DV)
class C;
  bit [1:0] a;
  bit b;
  covergroup cg with function sample(bit [1:0] fb, bit start, bit stop);
    cp_start : coverpoint start;
    cr : cross fb, start, stop { bins s0 = binsof(start) intersect {0}; }
  endgroup
  covergroup cg2;
    cp_action : coverpoint a { bins start = {1}; bins stop = {2}; }
    cp_b : coverpoint b;
    cr : cross cp_action, cp_b { bins w = binsof(cp_action.start); }
  endgroup
  function new; cg = new; cg2 = new; endfunction
endclass
module tb; C c = new; endmodule
