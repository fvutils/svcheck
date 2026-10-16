// A positive corpus for the contexts trial's rules (svcheck-plan P1.7; sv-contexts-trial.md):
// a small APB bench with the agent, env and test in one package, as a quick-start bench
// often is, and a monitor that raises an objection. Written for svcheck; the sources are
// in ctx-positive/. Read with -F.
--timescale 1ns/1ps
+define+UVM_NO_DPI
+incdir+../../packages/uvm-core/src
../../packages/uvm-core/src/uvm_pkg.sv
ctx-positive/apb_if.sv
ctx-positive/tb_pkg.sv
ctx-positive/tb_top.sv
--top tb_top
