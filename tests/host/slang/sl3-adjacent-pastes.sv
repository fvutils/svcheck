// SL-3 (patches/slang/README.md): two adjacent macro pastes paste once, so
// a name can be built around an empty argument. Reduced from CORE-V-VERIF's
// cv32e40p/tb/uvmt/uvmt_cv32e40p_macros.sv (RVFI_CSR_IDX_BIND).
`define P2(a, b) int a````b;
`define P3(a, sfx, idx) int a````idx````sfx``_q;
module top;
  `P2(x, 1)
  `P3(y, , 3)
  `P3(z, _h, 4)
  initial $display(x1, y3_q, z4_h_q);
endmodule
