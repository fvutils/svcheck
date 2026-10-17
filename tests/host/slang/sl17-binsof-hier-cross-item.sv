// SL-17 (lrm-relaxations, svcheck-plan P2.4b): binsof of a hierarchical cross item, slang's
// extension (-Wnonstandard-hierarchical-cross) (OpenTitan's sysrst_ctrl DV)
interface vi; logic x; endinterface
class Cfg; virtual vi vif; endclass
class I;
  Cfg cfg;
  logic y;
  covergroup cg;
    cp_y : coverpoint y;
    yx : cross cp_y, cfg.vif.x { ignore_bins i0 = binsof(cfg.vif.x) intersect {1}; }
  endgroup
  function new; cg = new; endfunction
endclass
module tb; I i = new; endmodule
