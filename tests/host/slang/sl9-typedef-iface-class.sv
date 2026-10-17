// SL-9 (svcheck-plan P2.3), a relaxation: typedef class forward-declaring an interface class (LRM 6.18)
typedef class X;
class user;
  X h;
endclass
interface class X;
  pure virtual function void f();
endclass
module tb; user u = new; endmodule
