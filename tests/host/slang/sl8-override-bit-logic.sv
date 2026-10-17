// SL-8 (svcheck-plan P2.3), a relaxation: virtual override with bit vs logic argument/return type (LRM 8.20)
class base;
  virtual function void f(logic [7:0] a); endfunction
  virtual function logic g(); return 0; endfunction
endclass
class derived extends base;
  virtual function void f(bit [7:0] a); endfunction
  virtual function bit g(); return 0; endfunction
endclass
module tb; derived d = new; endmodule
