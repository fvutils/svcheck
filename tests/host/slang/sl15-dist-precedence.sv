// SL-15 (lrm-relaxations, svcheck-plan P2.4b), a relaxation: a dist over a whole comparison,
// as the expression_or_dist grammar reads it (OpenTitan's keymgr DV)
class E;
  rand bit [3:0] a, b;
  constraint k { a == b dist {0 :/ 3, 1 :/ 1}; }
endclass
module tb; E e = new; endmodule
