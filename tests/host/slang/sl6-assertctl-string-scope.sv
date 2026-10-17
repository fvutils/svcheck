// SL-6 (svcheck-plan P2.3), a relaxation: assertion control tasks with a hierarchical scope as a string (VCS extension)
module dut; logic x; endmodule
module tb;
  dut u_dut();
  initial begin
    $assertoff(0, "tb.u_dut");
    $asserton(0, "tb.u_dut");
    $assertkill(0, "tb.u_dut");
    $assertcontrol(4, 15, 7, 0, "tb.u_dut");
  end
endmodule
