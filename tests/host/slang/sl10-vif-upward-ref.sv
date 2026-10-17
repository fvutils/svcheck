// SL-10 (svcheck-plan P2.3), a relaxation: interface with upward hierarchical reference, bound into DUT, used as virtual interface
interface probe_if();
  logic s;
  assign s = dut.sig;   // upward reference into the bind target
endinterface
module dut; logic sig; endmodule
module tb;
  dut dut();
  bind dut probe_if u_probe();
  virtual probe_if vif;
  initial vif = dut.u_probe;
endmodule
