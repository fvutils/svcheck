typedef enum logic [7:0] {A} e_t;
class base;
  virtual function void f(logic [7:0] a, int b, logic [7:0] c, e_t d); endfunction
endclass
class derived extends base;
  virtual function void f(bit [15:0] a, bit [31:0] b, logic signed [7:0] c, logic [7:0] d); endfunction
endclass
module tb; derived d = new; endmodule
