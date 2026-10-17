// SL-11 (svcheck-plan P2.3), a relaxation: queues of logic [7:0] vs bit [7:0] assigned without a cast
module tb;
  logic [7:0] q1[$];
  bit   [7:0] q2[$];
  initial begin
    q1 = q2;
    q2 = q1;
  end
endmodule
