// The fwvip VIPs of uvm-tb-proto alone: fwvip-common, fwvip-wb and fwvip-spi with
// no testbench, test or top (svcheck-plan P1.7, S-1/S-2). Nothing specialises their
// parameterised classes or instantiates their interfaces, so slang elaborates the
// interfaces as tops with their defaults, and a generic class only as its default
// specialisation. Read with -F, over the uvm-tb-proto snapshot (README.md).
--timescale 1ns/1ps
+define+UVM_NO_DPI
+incdir+../../packages/uvm-core/src
../../packages/uvm-core/src/uvm_pkg.sv
+incdir+../../packages/uvm-tb-proto/vip/fwprotocol-defs/verilog/rtl
+incdir+../../packages/uvm-tb-proto/vip/fwvip-common/src/vip
+incdir+../../packages/uvm-tb-proto/vip/fwvip-wb/src/vip/sv
+incdir+../../packages/uvm-tb-proto/vip/fwvip-wb/src/vip/uvm
+incdir+../../packages/uvm-tb-proto/vip/fwvip-spi/src/vip/sv
+incdir+../../packages/uvm-tb-proto/vip/fwvip-spi/src/vip/uvm
../../packages/uvm-tb-proto/vip/fwvip-common/src/vip/fwvip_pkg.sv
../../packages/uvm-tb-proto/vip/fwvip-common/src/vip/fwvip_ingress_fifo_if.sv
../../packages/uvm-tb-proto/vip/fwvip-common/src/vip/fwvip_egress_fifo_if.sv
../../packages/uvm-tb-proto/vip/fwvip-wb/src/vip/sv/fwvip_wb_bfm_pkg.sv
../../packages/uvm-tb-proto/vip/fwvip-wb/src/vip/sv/fwvip_wb_initiator_core.sv
../../packages/uvm-tb-proto/vip/fwvip-wb/src/vip/sv/fwvip_wb_initiator_if.sv
../../packages/uvm-tb-proto/vip/fwvip-wb/src/vip/sv/fwvip_wb_monitor_core.sv
../../packages/uvm-tb-proto/vip/fwvip-wb/src/vip/sv/fwvip_wb_monitor_if.sv
../../packages/uvm-tb-proto/vip/fwvip-wb/src/vip/sv/fwvip_wb_target_core.sv
../../packages/uvm-tb-proto/vip/fwvip-wb/src/vip/sv/fwvip_wb_target_if.sv
../../packages/uvm-tb-proto/vip/fwvip-wb/src/vip/uvm/fwvip_wb_pkg.sv
../../packages/uvm-tb-proto/vip/fwvip-spi/src/vip/sv/fwvip_spi_bfm_pkg.sv
../../packages/uvm-tb-proto/vip/fwvip-spi/src/vip/sv/fwvip_spi_initiator_core.sv
../../packages/uvm-tb-proto/vip/fwvip-spi/src/vip/sv/fwvip_spi_initiator_if.sv
../../packages/uvm-tb-proto/vip/fwvip-spi/src/vip/sv/fwvip_spi_monitor_core.sv
../../packages/uvm-tb-proto/vip/fwvip-spi/src/vip/sv/fwvip_spi_monitor_if.sv
../../packages/uvm-tb-proto/vip/fwvip-spi/src/vip/sv/fwvip_spi_target_core.sv
../../packages/uvm-tb-proto/vip/fwvip-spi/src/vip/sv/fwvip_spi_target_if.sv
../../packages/uvm-tb-proto/vip/fwvip-spi/src/vip/uvm/fwvip_spi_pkg.sv
