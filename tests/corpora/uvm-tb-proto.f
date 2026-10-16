// uvm-tb-proto's generated SPI-controller testbench (svcheck-plan P1.6b, PR-5), over
// the snapshot in packages/uvm-tb-proto (tests/corpora/README.md). Read with -F: paths
// are relative to here. Its flow.yaml compiles with Verilator; this list is svcheck's.
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
+incdir+../../packages/uvm-tb-proto/out/env
+incdir+../../packages/uvm-tb-proto/out/tests
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
../../packages/uvm-tb-proto/out/env/spi_ctl_env_pkg.sv
../../packages/uvm-tb-proto/out/tests/spi_ctl_tests_pkg.sv
../../packages/uvm-tb-proto/out/tb/spi_ctl_hdl_top.sv
../../packages/uvm-tb-proto/out/tb/spi_ctl_hvl_top.sv
--top spi_ctl_hdl_top,spi_ctl_hvl_top
