// The bus both benches drive (svcheck-plan P1.7, the contexts trial's positive corpus).
interface apb_if (input logic pclk);
  logic        psel;
  logic        penable;
  logic        pwrite;
  logic [31:0] paddr;
  logic [31:0] pwdata;
  logic [31:0] prdata;
  logic        pready;
endinterface
