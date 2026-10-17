// SL-5 (patches/slang/README.md): errors that depend on parameter values,
// from a generic class body checked with its parameters unset. Reduced from
// CORE-V-VERIF's lib/uvm_libs/uvml_mem/uvml_mem.sv and
// cv32e40p/env/uvme/cov/uvme_interrupt_covg_v2.sv. Nothing specialises m or c.
interface ifc #(parameter int W = 32) (input clk);
endinterface
package p;
class m #(int XLEN = 32);
  reg [7:0] mem [bit [XLEN-1:0]];
  function void load(string f); $readmemh(f, mem); endfunction
endclass
class c #(int W = 32);
  virtual ifc #(.W(W)) v;
  function void f(virtual ifc x); v = x; endfunction
endclass
endpackage
module top; logic clk; ifc i(clk); endmodule
