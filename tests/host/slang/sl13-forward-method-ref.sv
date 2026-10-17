// SL-13 (lrm-relaxations, svcheck-plan P2.4b), a relaxation: a class method that returns a
// value, called without parentheses before its declaration (OpenTitan's keymgr and kmac DV)
class D;
  task run; int x; x = get_op; endtask
  virtual function int get_op(); return 1; endfunction
endclass
module tb; D d = new; endmodule
