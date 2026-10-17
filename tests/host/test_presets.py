"""The built-in presets (svcheck-plan P2.6, Q-UVM3; rules/presets/): `--preset
NAME` or `preset:` in `--config`, a selection layer under the configuration's
and the command line's; a preset selects the UVM pack, sets rule options and
brings its house's naming conventions, which feed only SVCHK-UVM039.
"""
import json
import os
import subprocess

import pytest

from svcheck import native_path

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
UVM = os.path.join(ROOT, "packages", "uvm-core", "src")
pytestmark = pytest.mark.skipif(not os.path.isfile(os.path.join(UVM, "uvm_pkg.sv")),
                                reason="UVM is not in packages/ (ivpm update)")
PRESETS = ["core-v", "easier-uvm", "lowrisc-dv", "uvm-consensus"]

HDR = 'import uvm_pkg::*;\n`include "uvm_macros.svh"\n'
CTOR = "function new(string name, uvm_component parent); super.new(name, parent); endfunction"

# An agent package whose default sequence raises an objection,
# an env whose scoreboard waits a delay, a component constructor with
# defaults, and a bench that sets a vif for every component.
BENCH = HDR + """\
interface bus_if(input logic clk); endinterface
package bus_agent_pkg;
  import uvm_pkg::*;
  class bus_item extends uvm_sequence_item;
    `uvm_object_utils(bus_item)
    function new(string name = "bus_item"); super.new(name); endfunction
  endclass
  class bus_seq extends uvm_sequence #(bus_item);
    `uvm_object_utils(bus_seq)
    function new(string name = "bus_seq"); super.new(name); endfunction
    task body();
      uvm_phase p = get_starting_phase();
      if (p != null) p.raise_objection(this, "bus_seq");
      if (p != null) p.drop_objection(this, "bus_seq");
    endtask
  endclass
  class bus_agent extends uvm_agent;
    `uvm_component_utils(bus_agent)
    function new(string name = "bus_agent", uvm_component parent = null);
      super.new(name, parent);
    endfunction
  endclass
endpackage
package top_env_pkg;
  import uvm_pkg::*;
  import bus_agent_pkg::*;
  class top_sb extends uvm_scoreboard;
    `uvm_component_utils(top_sb)
    %(CTOR)s
    task run_phase(uvm_phase phase);
      #10ns;
    endtask
  endclass
  class top_env extends uvm_env;
    `uvm_component_utils(top_env)
    bus_agent agent;
    %(CTOR)s
  endclass
endpackage
module tb;
  import uvm_pkg::*;
  logic clk;
  bus_if bus(clk);
  initial begin
    uvm_config_db#(virtual bus_if)::set(null, "*.env.agent*", "vif", bus);
    run_test();
  end
endmodule
""" % {"CTOR": CTOR}


def svcheck(d, *args, files=None):
    """Runs svcheck in directory `d` on tb.sv (or `files`) with UVM."""
    files = files or {"tb.sv": BENCH}
    for name, text in files.items():
        with open(os.path.join(d, name), "w", encoding="utf-8") as fp:
            fp.write(text)
    cmd = [native_path(), "+incdir+" + UVM, os.path.join(UVM, "uvm_pkg.sv")]
    cmd += [os.path.join(d, n) for n in files if n.endswith(".sv")] + list(args)
    return subprocess.run(cmd, capture_output=True, text=True, cwd=d, timeout=120)


def codes(d, *args, files=None):
    r = svcheck(d, *args, "--format", "json", files=files)
    assert r.returncode in (0, 1), r.stderr
    return sorted({f["code"] for f in json.loads(r.stdout)["findings"]})


def test_unknown_preset_lists_them(tmp_path):
    r = svcheck(str(tmp_path), "--preset", "lowrisc")
    assert r.returncode == 2
    assert "unknown preset 'lowrisc'" in r.stderr
    for p in PRESETS:
        assert "\n  %s: " % p in r.stderr


def test_a_preset_selects_the_pack(tmp_path):
    d = str(tmp_path)
    # The default profile runs no pack rule; a preset runs the pack.
    assert codes(d, "--profile", "minimal") == []
    assert "SVCHK-UVM035" in codes(d, "--preset", "uvm-consensus")


def test_uvm_consensus_is_the_36(tmp_path):
    r = svcheck(str(tmp_path), "--preset", "uvm-consensus", "--show-config")
    out = json.loads(r.stdout)
    assert out["preset"] == "uvm-consensus"
    enabled = out["groups"][0]["enabled"]
    assert enabled == ["SVCHK-UVM%03d" % i for i in range(1, 37)]


def test_lowrisc_dv_leaves_out_035_and_the_project_can_put_it_back(tmp_path):
    d = str(tmp_path)
    assert "SVCHK-UVM035" not in codes(d, "--preset", "lowrisc-dv")
    # The command line's layer is over the preset's.
    assert "SVCHK-UVM035" in codes(d, "--preset", "lowrisc-dv", "--select", "SVCHK-UVM035")
    # So is the configuration's: a select there beats the preset's ignore.
    with open(os.path.join(d, "svcheck.yaml"), "w") as fp:
        fp.write("svcheck:\n  preset: lowrisc-dv\n  select: [SVCHK-UVM035]\n")
    assert "SVCHK-UVM035" in codes(d, "--config", "svcheck.yaml")
    with open(os.path.join(d, "svcheck.yaml"), "w") as fp:
        fp.write("svcheck:\n  preset: lowrisc-dv\n")
    assert "SVCHK-UVM035" not in codes(d, "--config", "svcheck.yaml")
    # --preset wins over the configuration's.
    assert "SVCHK-UVM035" in codes(d, "--config", "svcheck.yaml", "--preset", "core-v")


def test_core_v_accepts_defaulted_constructor_arguments(tmp_path):
    d = str(tmp_path)
    assert "SVCHK-UVM002" in codes(d, "--preset", "uvm-consensus")
    assert "SVCHK-UVM002" not in codes(d, "--preset", "core-v")
    # The configuration's options are over the preset's.
    with open(os.path.join(d, "svcheck.yaml"), "w") as fp:
        fp.write("svcheck:\n  rule:\n    SVCHK-UVM002: {allow_defaults: false}\n")
    assert "SVCHK-UVM002" in codes(d, "--preset", "core-v", "--config", "svcheck.yaml")


def test_easier_uvm_keeps_objections_and_interfaces_in_the_test(tmp_path):
    d = str(tmp_path)
    # The default sequence's objection, and the wildcard vif path.
    for c in ("SVCHK-UVM033", "SVCHK-UVM037"):
        assert c not in codes(d, "--select", "SVCHK-UVM")
        assert c in codes(d, "--preset", "easier-uvm")


AGENT_NAMED_ENV = HDR + """\
package bus_agent_pkg;
  import uvm_pkg::*;
  class bus_env extends uvm_env;
    `uvm_component_utils(bus_env)
    %(CTOR)s
  endclass
endpackage
package base_agent_pkg;
  import uvm_pkg::*;
  class base_agent extends uvm_agent;
    %(CTOR)s
  endclass
endpackage
package user_pkg;
  import uvm_pkg::*;
  import base_agent_pkg::*;
  class my_agent extends base_agent;
    `uvm_component_utils(my_agent)
    %(CTOR)s
  endclass
endpackage
""" % {"CTOR": CTOR}


def test_the_consistency_rule_reads_the_presets_conventions(tmp_path):
    d = str(tmp_path)
    files = {"tb.sv": AGENT_NAMED_ENV}
    r = svcheck(d, "--preset", "lowrisc-dv", "--format", "json", files=files)
    found = [f for f in json.loads(r.stdout)["findings"] if f["code"] == "SVCHK-UVM039"]
    # bus_agent_pkg holds an env. base_agent_pkg is a base library: not reported.
    assert [f["line"] for f in found] == [3]
    msg = found[0]["message"]
    assert msg.startswith("bus_agent_pkg: convention (svcheck:rules/presets/lowrisc-dv-contexts.yaml:")
    assert msg.endswith(") says 'agent', but content says 'env' on axis 'unit'; rename or move "
                        "it, or fix the tag")
    # No preset, no conventions; a preset of another house has other names.
    assert "SVCHK-UVM039" not in codes(d, "--select", "SVCHK-UVM", files=files)
    assert "SVCHK-UVM039" not in codes(d, "--preset", "core-v", files=files)


def test_a_preset_needs_the_model(tmp_path):
    r = svcheck(str(tmp_path), "--preset", "core-v", "--semantic", "none")
    assert r.returncode == 2 and "needs the model" in r.stderr


def test_preset_in_a_configuration_must_be_a_name(tmp_path):
    d = str(tmp_path)
    with open(os.path.join(d, "svcheck.yaml"), "w") as fp:
        fp.write("svcheck:\n  preset: [core-v]\n")
    r = svcheck(d, "--config", "svcheck.yaml")
    assert r.returncode == 2 and "svcheck.preset: not a preset name" in r.stderr
