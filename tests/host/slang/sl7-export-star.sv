// SL-7 (svcheck-plan P2.3), a relaxation: export pkg::* passes on only names P references (LRM 26.6)
package a_pkg;
  typedef int a_t;
  parameter int A = 1;
endpackage
package p_pkg;
  import a_pkg::*;
  export a_pkg::*;
endpackage
module tb;
  import p_pkg::*;
  a_t v = A;
endmodule
