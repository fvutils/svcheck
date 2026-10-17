// SL-11 (lrm-relaxations, svcheck-plan P2.4b), a relaxation: a bit [7:0] queue output
// argument copied out to a logic [7:0] queue (OpenTitan's spi_agent)
class G;
  logic [7:0] data[$];
  task issue(input bit [7:0] i[$], output bit [7:0] o[$]); o = i; endtask
  task run; bit [7:0] q[$]; issue(q, data); endtask
endclass
module tb; G g = new; endmodule
