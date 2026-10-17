// CORE-V-VERIF's CV32E40P UVM bench (uvmt_cv32e40p_tb), core-v-verif cv32e40p_v1.8.3 with
// the cv32e40p RTL and svlib its ExternalRepos.mk names (svcheck-plan P2.1; ivpm's `corpora`
// dep-set). Written by scripts/corpora/corev.py from the bench's .flist files: do not edit.
// Defines as the makefiles' default: CV32E40P_ASSERT_ON (uvmt.mk), NO_PULP (cfg/default.yaml),
// CV32E40P_TRACE_EXECUTION and CV32E40P_RVFI (dsim.mk, the flow with no ISS). UVM 1.2, as
// the simulators bundle. Read with -F: paths are relative to here.
// One compilation unit, as dsim compiles it: the tb uses the uvmt package's macros.
--single-unit
--top uvmt_cv32e40p_tb
+define+CV32E40P_ASSERT_ON
+define+NO_PULP
+define+CV32E40P_TRACE_EXECUTION
+define+CV32E40P_RVFI
// slang ranks solve-before in a foreach an error (§18.5.10); the interrupt agent has 2.
-Wno-nested-solve-before
+incdir+../../packages/uvm-1.2/src
../../packages/uvm-1.2/src/uvm_pkg.sv
// ../../packages/cv32e40p/cv32e40p_manifest.flist
+incdir+../../packages/cv32e40p/rtl/include
+incdir+../../packages/cv32e40p/bhv
+incdir+../../packages/cv32e40p/bhv/include
+incdir+../../packages/cv32e40p/sva
../../packages/cv32e40p/rtl/include/cv32e40p_apu_core_pkg.sv
../../packages/cv32e40p/rtl/include/cv32e40p_fpu_pkg.sv
../../packages/cv32e40p/rtl/include/cv32e40p_pkg.sv
../../packages/cv32e40p/rtl/cv32e40p_if_stage.sv
../../packages/cv32e40p/rtl/cv32e40p_cs_registers.sv
../../packages/cv32e40p/rtl/cv32e40p_register_file_ff.sv
../../packages/cv32e40p/rtl/cv32e40p_load_store_unit.sv
../../packages/cv32e40p/rtl/cv32e40p_id_stage.sv
../../packages/cv32e40p/rtl/cv32e40p_aligner.sv
../../packages/cv32e40p/rtl/cv32e40p_decoder.sv
../../packages/cv32e40p/rtl/cv32e40p_compressed_decoder.sv
../../packages/cv32e40p/rtl/cv32e40p_fifo.sv
../../packages/cv32e40p/rtl/cv32e40p_prefetch_buffer.sv
../../packages/cv32e40p/rtl/cv32e40p_hwloop_regs.sv
../../packages/cv32e40p/rtl/cv32e40p_mult.sv
../../packages/cv32e40p/rtl/cv32e40p_int_controller.sv
../../packages/cv32e40p/rtl/cv32e40p_ex_stage.sv
../../packages/cv32e40p/rtl/cv32e40p_alu_div.sv
../../packages/cv32e40p/rtl/cv32e40p_alu.sv
../../packages/cv32e40p/rtl/cv32e40p_ff_one.sv
../../packages/cv32e40p/rtl/cv32e40p_popcnt.sv
../../packages/cv32e40p/rtl/cv32e40p_apu_disp.sv
../../packages/cv32e40p/rtl/cv32e40p_controller.sv
../../packages/cv32e40p/rtl/cv32e40p_obi_interface.sv
../../packages/cv32e40p/rtl/cv32e40p_prefetch_controller.sv
../../packages/cv32e40p/rtl/cv32e40p_sleep_unit.sv
../../packages/cv32e40p/rtl/cv32e40p_core.sv
../../packages/cv32e40p/rtl/cv32e40p_top.sv
../../packages/cv32e40p/bhv/cv32e40p_sim_clock_gate.sv
../../packages/cv32e40p/bhv/include/cv32e40p_tracer_pkg.sv
../../packages/cv32e40p/bhv/cv32e40p_tb_wrapper.sv
// ../../packages/core-v-verif/cv32e40p/tb/uvmt/uvmt_cv32e40p.flist
// ../../packages/core-v-verif/lib/uvm_libs/uvml_hrtbt/uvml_hrtbt_pkg.flist
+incdir+../../packages/core-v-verif/lib/uvm_libs/uvml_hrtbt
../../packages/core-v-verif/lib/uvm_libs/uvml_hrtbt/uvml_hrtbt_pkg.sv
// ../../packages/core-v-verif/lib/uvm_libs/uvml_trn/uvml_trn_pkg.flist
+incdir+../../packages/core-v-verif/lib/uvm_libs/uvml_trn
../../packages/core-v-verif/lib/uvm_libs/uvml_trn/uvml_trn_pkg.sv
// ../../packages/core-v-verif/lib/uvm_libs/uvml_logs/uvml_logs_pkg.flist
+incdir+../../packages/core-v-verif/lib/uvm_libs/uvml_logs
../../packages/core-v-verif/lib/uvm_libs/uvml_logs/uvml_logs_pkg.sv
// ../../packages/core-v-verif/lib/uvm_libs/uvml_sb/uvml_sb_pkg.flist
+incdir+../../packages/core-v-verif/lib/uvm_libs/uvml_sb
../../packages/core-v-verif/lib/uvm_libs/uvml_sb/uvml_sb_pkg.sv
// ../../packages/core-v-verif/lib/uvm_libs/uvml_mem/uvml_mem_pkg.flist
+incdir+../../packages/core-v-verif/lib/uvm_libs/uvml_mem
../../packages/core-v-verif/lib/uvm_libs/uvml_mem/uvml_mem_pkg.sv
// ../../packages/core-v-verif/lib/uvm_agents/uvma_core_cntrl/uvma_core_cntrl_pkg.flist
+incdir+../../packages/core-v-verif/lib/uvm_agents/uvma_core_cntrl
../../packages/core-v-verif/lib/uvm_agents/uvma_core_cntrl/uvma_core_cntrl_pkg.sv
// ../../packages/core-v-verif/lib/uvm_agents/uvma_obi_memory/src/uvma_obi_memory_pkg.flist
+incdir+../../packages/core-v-verif/lib/uvm_agents/uvma_obi_memory/src
+incdir+../../packages/core-v-verif/lib/uvm_agents/uvma_obi_memory/src/comps
+incdir+../../packages/core-v-verif/lib/uvm_agents/uvma_obi_memory/src/obj
+incdir+../../packages/core-v-verif/lib/uvm_agents/uvma_obi_memory/src/seq
../../packages/core-v-verif/lib/uvm_agents/uvma_obi_memory/src/uvma_obi_memory_pkg.sv
// ../../packages/core-v-verif/lib/uvm_agents/uvma_rvfi/uvma_rvfi_pkg.flist
+incdir+../../packages/core-v-verif/lib/uvm_agents/uvma_rvfi
../../packages/core-v-verif/lib/uvm_agents/uvma_rvfi/uvma_rvfi_pkg.sv
// ../../packages/core-v-verif/lib/uvm_agents/uvma_clknrst/uvma_clknrst_pkg.flist
+incdir+../../packages/core-v-verif/lib/uvm_agents/uvma_clknrst
+incdir+../../packages/core-v-verif/lib/uvm_agents/uvma_clknrst/cov
+incdir+../../packages/core-v-verif/lib/uvm_agents/uvma_clknrst/seq
../../packages/core-v-verif/lib/uvm_agents/uvma_clknrst/uvma_clknrst_pkg.sv
// ../../packages/core-v-verif/lib/uvm_agents/uvma_interrupt/uvma_interrupt_pkg.flist
+incdir+../../packages/core-v-verif/lib/uvm_agents/uvma_interrupt
+incdir+../../packages/core-v-verif/lib/uvm_agents/uvma_interrupt/cov
+incdir+../../packages/core-v-verif/lib/uvm_agents/uvma_interrupt/seq
../../packages/core-v-verif/lib/uvm_agents/uvma_interrupt/uvma_interrupt_pkg.sv
// ../../packages/core-v-verif/lib/uvm_agents/uvma_obi_memory/src/uvma_obi_memory_pkg.flist
// ../../packages/core-v-verif/lib/uvm_agents/uvma_debug/uvma_debug_pkg.flist
+incdir+../../packages/core-v-verif/lib/uvm_agents/uvma_debug
+incdir+../../packages/core-v-verif/lib/uvm_agents/uvma_debug/cov
+incdir+../../packages/core-v-verif/lib/uvm_agents/uvma_debug/seq
../../packages/core-v-verif/lib/uvm_agents/uvma_debug/uvma_debug_pkg.sv
// ../../packages/core-v-verif/cv32e40p/vendor_lib/verilab/svlib_pkg.flist
+incdir+../../packages/svlib/svlib/src
../../packages/svlib/svlib/src/svlib_pkg.sv
// ../../packages/core-v-verif/cv32e40p/env/uvme/uvme_cv32e40p_pkg.flist
+incdir+../../packages/core-v-verif/cv32e40p/env/uvme
+incdir+../../packages/core-v-verif/cv32e40p/env/uvme/cov
+incdir+../../packages/core-v-verif/cv32e40p/env/uvme/vseq
../../packages/core-v-verif/cv32e40p/env/uvme/uvme_cv32e40p_pkg.sv
+incdir+../../packages/core-v-verif/cv32e40p/tb/uvmt
+incdir+../../packages/core-v-verif/cv32e40p/tests/uvmt
+incdir+../../packages/core-v-verif/cv32e40p/tests/uvmt/base-tests
+incdir+../../packages/core-v-verif/cv32e40p/tests/uvmt/compliance-tests
+incdir+../../packages/core-v-verif/cv32e40p/tests/uvmt/vseq
+incdir+../../packages/core-v-verif/cv32e40p/tb
../../packages/core-v-verif/cv32e40p/tb/uvmt/uvmt_cv32e40p_pkg.sv
../../packages/core-v-verif/cv32e40p/tb/uvmt/uvmt_cv32e40p_dut_wrap.sv
../../packages/core-v-verif/cv32e40p/tb/uvmt/uvmt_cv32e40p_tb.sv
../../packages/core-v-verif/cv32e40p/tb/uvmt/uvmt_cv32e40p_interrupt_assert.sv
../../packages/core-v-verif/cv32e40p/tb/uvmt/uvmt_cv32e40p_debug_assert.sv
