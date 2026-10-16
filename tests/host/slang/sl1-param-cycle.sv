// SL-1 (patches/slang/README.md): slang 11.0 and 12.0 report "cyclic
// dependency found when determining value of 'W'" on this legal code, unless
// patched; Verilator 5.041/5.052 accept it. Found in fw-wb-dma (svcheck-plan
// P1-24): wb_initiator_xtor u_mst0/u_mst1, a class naming u_mst1.u_if.
//
// Needs all of: two instances of a module holding the interface; W derived
// from another parameter and used in a port; a virtual-interface reference to
// the SECOND instance's interface (the first is fine; an initial block
// triggers it as well as a class method).
//
//   svcheck sl1-param-cycle.sv --top top
interface x_if #(parameter int A = 32, parameter int W = A + 1) (output reg [W-1:0] d);
endinterface
module x_xtor; x_if u_if (); endmodule
module top;
  x_xtor u_m0 ();
  x_xtor u_m1 ();
  class c;
    function void f(virtual x_if #(32) v);
      f(u_m1.u_if);
    endfunction
  endclass
endmodule
