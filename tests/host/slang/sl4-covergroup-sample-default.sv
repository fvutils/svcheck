// SL-4 (patches/slang/README.md): a covergroup's sample() argument with a
// default names its class's own type and enum value. Reduced from
// CORE-V-VERIF's cv32e40p/env/uvme/cov/uvme_rv32x_hwloop_covg.sv.
class c;
  typedef enum int {A = 0, B, TOTAL = 2} loc_t;
  covergroup cg with function sample(loc_t l = TOTAL);
    cp: coverpoint l;
  endgroup
  function new; cg = new; endfunction
endclass
module top; c o = new; endmodule
