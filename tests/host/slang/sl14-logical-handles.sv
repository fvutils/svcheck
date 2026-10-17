// SL-14 (lrm-relaxations, svcheck-plan P2.4b), a relaxation: class handles as operands of the
// binary logical operators (OpenTitan's spi_device DV)
class F; endclass
class H;
  F a, b;
  int n;
  task run; if (a && b || !a) n = 1; endtask
endclass
module tb; H h = new; endmodule
