module tb_top;
  import uvm_pkg::*;
  import tb_pkg::*;
  logic clk = 0;
  always #5 clk = ~clk;
  apb_if bus(clk);
  initial begin
    uvm_config_db #(virtual apb_if)::set(null, "*", "vif", bus);
    run_test("apb_test");
  end
endmodule
