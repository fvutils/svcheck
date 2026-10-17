module tb;
  logic [7:0] q1[$]; bit [15:0] q3[$]; bit [7:0] fa[4]; logic [7:0] da[];
  function automatic int f(logic [7:0] a[$]); return a.size(); endfunction
  initial begin
    da = fa;  fa = da;  q1 = fa;
    void'(f(fa));
    q1 = q3;
  end
  localparam bit [7:0] P[2] = '{8'h1, 8'h2};
  localparam logic [7:0] Q[2] = P;
  initial $display("%p", Q);
endmodule
