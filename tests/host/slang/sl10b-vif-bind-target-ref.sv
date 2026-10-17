// SL-10 (lrm-relaxations, svcheck-plan P2.4b), a relaxation: a bound interface whose names are
// relative to its bind target (u_core), used as a virtual interface; and a set covergroup over
// such a name (the virtual copy gives it an error type, which must not be reported again)
module core; logic [3:0] depth; endmodule
module dutm; core u_core(); endmodule
interface cov_if;
  typedef bit [3:0] m_t;
  typedef m_t q_t[$];
  logic [3:0] d;
  assign d = u_core.depth;
  function automatic q_t masks(int n);
    q_t r;
    for (int i = 0; i < n; i++) r.push_back(m_t'(1 << i));
    return r;
  endfunction
  covergroup cg (int n) with function sample();
    cp: coverpoint u_core.depth { bins s[] = masks(n); }
  endgroup
  cg c = new(2);
endinterface
module tb;
  dutm dut();
  bind dutm cov_if u_cov_if();
  virtual cov_if v;
  initial v = dut.u_cov_if;
endmodule
