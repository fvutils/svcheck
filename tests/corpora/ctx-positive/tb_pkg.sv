// One package for the whole bench, as a quick-start bench often is: the agent,
// the env and the test together. SVCHK-UVM038 should report the package; 037 the
// monitor's objection, and not the scoreboard's or the sequence's.
package tb_pkg;
  import uvm_pkg::*;
  `include "uvm_macros.svh"

  class apb_item extends uvm_sequence_item;
    rand bit        write;
    rand bit [31:0] addr;
    rand bit [31:0] data;
    `uvm_object_utils(apb_item)
    function new(string name = "apb_item"); super.new(name); endfunction
  endclass

  class apb_seq extends uvm_sequence #(apb_item);
    `uvm_object_utils(apb_seq)
    function new(string name = "apb_seq"); super.new(name); endfunction
    task body();
      repeat (10) begin
        apb_item it = apb_item::type_id::create("it");
        start_item(it);
        if (!it.randomize()) `uvm_error("SEQ", "randomize failed")
        finish_item(it);
      end
    endtask
  endclass

  class apb_driver extends uvm_driver #(apb_item);
    `uvm_component_utils(apb_driver)
    virtual apb_if vif;
    function new(string name, uvm_component parent); super.new(name, parent); endfunction
    function void build_phase(uvm_phase phase);
      super.build_phase(phase);
      if (!uvm_config_db #(virtual apb_if)::get(this, "", "vif", vif))
        `uvm_fatal("NOVIF", "no vif")
    endfunction
    task run_phase(uvm_phase phase);
      forever begin
        seq_item_port.get_next_item(req);
        @(posedge vif.pclk);
        vif.psel <= 1; vif.pwrite <= req.write; vif.paddr <= req.addr; vif.pwdata <= req.data;
        @(posedge vif.pclk);
        vif.penable <= 1;
        do @(posedge vif.pclk); while (!vif.pready);
        vif.psel <= 0; vif.penable <= 0;
        seq_item_port.item_done();
      end
    endtask
  endclass

  class apb_monitor extends uvm_monitor;
    `uvm_component_utils(apb_monitor)
    virtual apb_if vif;
    uvm_analysis_port #(apb_item) ap;
    function new(string name, uvm_component parent); super.new(name, parent); endfunction
    function void build_phase(uvm_phase phase);
      super.build_phase(phase);
      ap = new("ap", this);
      if (!uvm_config_db #(virtual apb_if)::get(this, "", "vif", vif))
        `uvm_fatal("NOVIF", "no vif")
    endfunction
    task run_phase(uvm_phase phase);
      forever begin
        @(posedge vif.pclk);
        if (vif.psel && vif.penable && vif.pready) begin
          apb_item it = apb_item::type_id::create("it");
          phase.raise_objection(this);
          it.write = vif.pwrite; it.addr = vif.paddr;
          it.data = vif.pwrite ? vif.pwdata : vif.prdata;
          ap.write(it);
          phase.drop_objection(this);
        end
      end
    endtask
  endclass

  class apb_agent extends uvm_agent;
    `uvm_component_utils(apb_agent)
    apb_driver                drv;
    apb_monitor               mon;
    uvm_sequencer #(apb_item) sqr;
    function new(string name, uvm_component parent); super.new(name, parent); endfunction
    function void build_phase(uvm_phase phase);
      super.build_phase(phase);
      mon = apb_monitor::type_id::create("mon", this);
      drv = apb_driver::type_id::create("drv", this);
      sqr = uvm_sequencer #(apb_item)::type_id::create("sqr", this);
    endfunction
    function void connect_phase(uvm_phase phase);
      drv.seq_item_port.connect(sqr.seq_item_export);
    endfunction
  endclass

  // A scoreboard and a sequence that raise objections: neither is in an
  // agent, so SVCHK-UVM037 must not report them, though the package holds an agent
  // (svcheck-plan P2.4: in a mixed package a class's context is its own).
  class apb_scoreboard extends uvm_scoreboard;
    `uvm_component_utils(apb_scoreboard)
    function new(string name, uvm_component parent); super.new(name, parent); endfunction
    task run_phase(uvm_phase phase);
      phase.raise_objection(this);
      #100ns;
      phase.drop_objection(this);
    endtask
  endclass

  class apb_long_seq extends apb_seq;
    `uvm_object_utils(apb_long_seq)
    function new(string name = "apb_long_seq"); super.new(name); endfunction
    task body();
      uvm_phase p = get_starting_phase();
      if (p != null) p.raise_objection(this);
      super.body();
      if (p != null) p.drop_objection(this);
    endtask
  endclass

  class apb_env extends uvm_env;
    `uvm_component_utils(apb_env)
    apb_agent agent;
    function new(string name, uvm_component parent); super.new(name, parent); endfunction
    function void build_phase(uvm_phase phase);
      super.build_phase(phase);
      agent = apb_agent::type_id::create("agent", this);
    endfunction
  endclass

  class apb_test extends uvm_test;
    `uvm_component_utils(apb_test)
    apb_env env;
    function new(string name, uvm_component parent); super.new(name, parent); endfunction
    function void build_phase(uvm_phase phase);
      super.build_phase(phase);
      env = apb_env::type_id::create("env", this);
    endfunction
    task run_phase(uvm_phase phase);
      apb_seq seq = apb_seq::type_id::create("seq");
      phase.raise_objection(this);
      seq.start(env.agent.sqr);
      phase.drop_objection(this);
    endtask
  endclass
endpackage
