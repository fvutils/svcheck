"""Contexts (svcheck-plan P1.4, R-6; sv-contexts.md): the `contexts:`
classifiers, the classify stage (src/engine/run/Classify.cpp), the host's
units and import edges (src/host/SvContext.cpp), the `// svcheck context:`
tag, and `context(n)`. Experimental until the P1.6b trial settles it.
"""
import os
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tests", "engine"))

from hooks import EngineHooks, check_examples  # noqa: E402

UVM = os.path.join(ROOT, "packages", "uvm-core", "src")
pytestmark = pytest.mark.skipif(not os.path.isfile(os.path.join(UVM, "uvm_pkg.sv")),
                                reason="UVM is not in packages/ (ivpm update)")


@pytest.fixture(scope="module")
def model():
    h = EngineHooks(semantic="model", uvm=UVM)
    yield h
    h.close()


# A UVM vocabulary, as a pack would write it (helpers and classifiers).
VOCAB = """\
  define:
    is(c, base): derives_from(c, "uvm_pkg::" + base)
    is_config_db(t): t != null && t.classDecl != null && t.classDecl.qname == "uvm_pkg::uvm_config_db"
  contexts:
    - label: uvm
      axis: methodology
      carrier: Package
      when: >-
        !carrier.inLibrary && carrier.imports.exists(p, p.name == "uvm_pkg")
    - label: agent
      axis: unit
      carrier: Package
      when: '!carrier.inLibrary && classes(carrier).exists(c, is(c, "uvm_agent"))'
      joins: >-
        calls_in(carrier).filter(c, c.name == "get" && is_config_db(c.classType)
          && c.classType.args.size() > 0 && c.classType.args[0].isVirtualInterface)
        .map(c, c.classType.args[0].ifaceDecl)
    - label: env
      axis: unit
      carrier: Package
      when: '!carrier.inLibrary && classes(carrier).exists(c, is(c, "uvm_env"))'
    - label: agent
      axis: unit
      carrier: Package
      strength: weak
      when: '!carrier.inLibrary && carrier.name.endsWith("_agent_pkg")'
    - label: hdl
      axis: partition
      carrier: Definition
      when: carrier.kind == DefinitionKind.Interface
"""


def _file(rules, vocab=VOCAB):
    return "sv-rules:\n" + vocab + "  rules:\n" + (rules or "    []\n")


def _rule(rid, match, where, message="m"):
    return ("    - id: %s\n"
            "      name: r-%s\n"
            "      tier: suspicious\n"
            "      summary: s\n"
            "      message: \"%s\"\n"
            "      match: { kind: %s }\n"
            "      where: >-\n"
            "        %s\n" % (rid, rid.lower(), message, match, where))


SRC = """\
import uvm_pkg::*;
interface bus_if; logic clk; endinterface
package bus_agent_pkg;
  import uvm_pkg::*;
  class bus_drv extends uvm_driver;
    virtual bus_if vif;
    function new(string name, uvm_component parent); super.new(name, parent); endfunction
    function void build_phase(uvm_phase phase);
      void'(uvm_config_db#(virtual bus_if)::get(this, "", "vif", vif));
    endfunction
  endclass
  class bus_agent extends uvm_agent;
    function new(string name, uvm_component parent); super.new(name, parent); endfunction
  endclass
endpackage
package top_env_pkg;
  import uvm_pkg::*;
  import bus_agent_pkg::*;
  class top_env extends uvm_env;
    function new(string name, uvm_component parent); super.new(name, parent); endfunction
  endclass
endpackage
package lone_agent_pkg;
endpackage
"""


def _labels(r):
    """The labels, by axis (in the order the classifiers name them), then
    carrier kind and line: carriers are in walk order, and the model has
    packages in slang's compilation order, not the source's."""
    axes = []
    for c in r["contexts"]:
        if c["axis"] not in axes:
            axes.append(c["axis"])
    return sorted([(c["kind"], c["line"], c["axis"], c["label"], c["unit"], c["source"])
                   for c in r["contexts"]],
                  key=lambda l: (axes.index(l[2]), l[0] != "Package", l[1]))


def _run(hooks, rules, src=SRC, vocab=VOCAB):
    r = hooks.lint_run("ctx.yaml", _file(rules, vocab), src)
    assert r["loaded"], r["diagnostics"]
    assert [d for d in r["diagnostics"] if d["severity"] == "error"] == [], r["diagnostics"]
    return r


def test_classify_labels_by_axis_with_their_sources(model):
    r = _run(model, None)
    assert _labels(r) == [
        ("Package", 3, "methodology", "uvm", "bus_agent_pkg", "Content"),
        ("Package", 16, "methodology", "uvm", "top_env_pkg", "Content"),
        ("Package", 3, "unit", "agent", "bus_agent_pkg", "Content"),
        ("Package", 16, "unit", "env", "top_env_pkg", "Content"),
        # Convention alone, with no content to say otherwise.
        ("Package", 23, "unit", "agent", "lone_agent_pkg", "Convention"),
        # Joined to the agent by its driver's config_db get of the interface.
        ("Definition", 2, "unit", "agent", "bus_agent_pkg", "Content"),
        ("Definition", 2, "partition", "hdl", "bus_if", "Content"),
    ]
    assert r["context_notes"] == []


def test_units_group_their_carriers_and_imports_are_edges(model):
    rules = (_rule("ACME001", "Unit",
                   "node.carriers.map(c, c.name) == ['bus_agent_pkg', 'bus_if'] && "
                   "node.labels == ['uvm', 'agent']", "${node.name}")
             + _rule("ACME002", "UnitDep", "true",
                     "${node.from.name} -> ${node.to.name}"))
    r = _run(model, rules)
    msgs = sorted((f["id"], f["msg"]) for f in r["findings"])
    assert msgs == [("ACME001", "bus_agent_pkg"),
                    ("ACME002", "top_env_pkg -> bus_agent_pkg")]


LONE_AGENT = """package lone_agent_pkg;
  import uvm_pkg::*;
  import top_env_pkg::*;
  class lone_agent extends uvm_agent;
    function new(string name, uvm_component parent); super.new(name, parent); endfunction
  endclass
"""


def test_a_boundary_rule_on_unit_edges(model):
    # An agent must not import an env (sv-contexts.md §4.2).
    src = SRC.replace("package lone_agent_pkg;\n", LONE_AGENT)
    rules = _rule("ACME003", "UnitDep",
                  "'agent' in node.from.labels && 'env' in node.to.labels",
                  "${node.from.name} depends on ${node.to.name}")
    r = _run(model, rules, src)
    assert [f["msg"] for f in r["findings"]] == ["lone_agent_pkg depends on top_env_pkg"]


def test_convention_alone_never_selects(model):
    # Q-CX4 (svcheck-plan P2.4): lone_agent_pkg is an agent by its name only.
    # The label is kept apart, in conventionLabels, and gives the unit no kind,
    # so neither a boundary rule nor applies_to sees it.
    src = SRC.replace("package lone_agent_pkg;\n",
                      "package lone_agent_pkg;\n  import top_env_pkg::*;\n")
    rules = (_rule("ACME003", "UnitDep",
                   "'agent' in node.from.labels && 'env' in node.to.labels",
                   "${node.from.name} depends on ${node.to.name}")
             + _rule("ACME010", "Unit", "node.conventionLabels.size() > 0",
                     "${node.name}: ${node.labels.size()} ${node.conventionLabels[0]}"))
    r = _run(model, rules, src)
    assert [f["msg"] for f in r["findings"]] == ["lone_agent_pkg: 0 agent"]


def test_context_of_a_node_is_its_innermost_carriers_unit(model):
    rules = _rule("ACME004", "Call",
                  "node.name == 'get' && 'agent' in context(node, 'unit').labels",
                  "${context(node).name}")
    r = _run(model, rules)
    assert [(f["line"], f["msg"]) for f in r["findings"]] == [(9, "bus_agent_pkg")]


def test_a_tag_wins_and_disagreeing_content_is_noted(model):
    src = SRC.replace("package top_env_pkg;", "// svcheck context: agent(soc)\npackage top_env_pkg;")
    r = _run(model, None, src)
    unit = [l for l in _labels(r) if l[2] == "unit" and l[0] == "Package" and l[1] == 17]
    assert unit == [("Package", 17, "unit", "agent", "soc", "Declared")]
    (note,) = r["context_notes"]
    assert note["line"] == 17
    assert note["message"].startswith("content (ctx.yaml:")
    assert note["message"].endswith("says 'env', but the tag says 'agent' on axis 'unit'")


def test_a_tag_with_an_unknown_label_is_noted(model):
    src = SRC.replace("package lone_agent_pkg;", "/* svcheck context: widget */ package lone_agent_pkg;")
    r = _run(model, None, src)
    assert [n["message"] for n in r["context_notes"]] == [
        "snippet.sv:23: context tag 'widget' is a label no loaded classifier has, so it has no "
        "axis; it is ignored"]


def test_content_outranks_a_disagreeing_convention(model):
    src = SRC.replace("top_env_pkg", "top_agent_pkg")
    r = _run(model, None, src)
    unit = [l[3:] for l in _labels(r) if l[2] == "unit" and l[1] == 16]
    assert unit == [("env", "top_agent_pkg", "Content")]
    (note,) = r["context_notes"]
    assert "convention" in note["message"] and "says 'agent', but content says 'env'" in (
        note["message"])


def test_mixed_carriers_sum_and_classes_keep_their_own(model):
    # One package with an agent class and an env class (Q-CX3).
    vocab = VOCAB.replace("""    - label: agent
      axis: unit
      carrier: Package
      when: '!carrier.inLibrary && classes(carrier).exists(c, is(c, "uvm_agent"))'""",
                          """    - label: agent
      axis: unit
      carrier: Class
      when: '!carrier.inLibrary && is(carrier, "uvm_agent")'""").replace(
        """    - label: env
      axis: unit
      carrier: Package
      when: '!carrier.inLibrary && classes(carrier).exists(c, is(c, "uvm_env"))'""",
        """    - label: env
      axis: unit
      carrier: Class
      when: '!carrier.inLibrary && is(carrier, "uvm_env")'""")
    assert vocab.count("carrier: Class") == 2
    src = ("import uvm_pkg::*;\n"
           "package tb_pkg;\n"
           "  import uvm_pkg::*;\n"
           "  class a extends uvm_agent;\n"
           "    function new(string name, uvm_component parent); super.new(name, parent); endfunction\n"
           "  endclass\n"
           "  class e extends uvm_env;\n"
           "    function new(string name, uvm_component parent); super.new(name, parent); endfunction\n"
           "  endclass\n"
           "  int shared;\n"
           "endpackage\n")
    rules = _rule("ACME005", "Decl", "node.name in ['new', 'shared'] && context(node, 'unit') != null",
                  "${node.qname}: ${context(node, 'unit').name}")
    r = _run(model, rules, src, vocab)
    pkg = [l[3:] for l in _labels(r) if l[0] == "Package" and l[2] == "unit"]
    assert pkg == [("agent", "tb_pkg", "Nested"), ("env", "tb_pkg", "Nested")]
    assert sorted(f["msg"] for f in r["findings"]) == [
        "tb_pkg::a::new: a", "tb_pkg::e::new: e", "tb_pkg::shared: tb_pkg"]


def test_a_later_axis_reads_an_earlier_one(model):
    vocab = VOCAB + """\
    - label: uvm-unit
      axis: kind
      carrier: Package
      when: context(carrier, 'methodology') != null && 'uvm' in context(carrier, 'methodology').labels
"""
    r = _run(model, None, SRC, vocab)
    kinds = [l[4] for l in _labels(r) if l[2] == "kind"]
    assert kinds == ["bus_agent_pkg", "top_env_pkg"]


def test_a_failing_classifier_is_noted_and_skipped(model):
    vocab = VOCAB + """\
    - label: broken
      axis: unit
      carrier: Package
      when: carrier.items[99] != null
"""
    r = _run(model, None, SRC, vocab)
    msgs = [n["message"] for n in r["context_notes"]]
    assert len(msgs) == 1 and msgs[0].startswith("classifier 'broken' (ctx.yaml:")
    assert "failed in its when" in msgs[0]


@pytest.mark.parametrize("classifier,msg", [
    ("    - { label: a, axis: u, carrier: Package }\n", "a classifier needs a 'when'"),
    ("    - { label: a, axis: u, when: 'true' }\n", "a classifier needs a 'carrier'"),
    ("    - { label: a, axis: u, carrier: Packag, when: 'true' }\n", "unknown node kind 'Packag'"),
    ("    - { label: a, axis: u, carrier: Package, strength: hard, when: 'true' }\n",
     "'strength' is 'strong'"),
    ("    - { label: a, axis: u, carrier: Package, when: carrier.nope }\n", "nope"),
    ("    - { axis: u, carrier: Package, when: 'true' }\n", "needs a 'label'"),
])
def test_classifier_errors(model, classifier, msg):
    r = model.lint_run("ctx.yaml", "sv-rules:\n  contexts:\n" + classifier + "  rules: []\n",
                       "package p; endpackage\n")
    errors = [d["message"] for d in r["diagnostics"] if d["severity"] == "error"]
    assert any(msg in e for e in errors), errors


def test_an_example_runs_its_files_classifiers(model):
    rules = _rule("ACME006", "Call",
                  "node.name == 'raise_objection' && context(node, 'unit') != null && "
                  "'agent' in context(node, 'unit').labels",
                  "objection in an agent")
    rules += """\
      examples:
        - code: |
            import uvm_pkg::*;
            package x_agent_pkg;
              import uvm_pkg::*;
              class x_agent extends uvm_agent;
                function new(string name, uvm_component parent); super.new(name, parent); endfunction
                task run_phase(uvm_phase phase);
                  phase.raise_objection(this);   // expect: ACME006
                endtask
              endclass
            endpackage
        - code: |
            import uvm_pkg::*;
            package x_env_pkg;
              import uvm_pkg::*;
              class x_env extends uvm_env;
                function new(string name, uvm_component parent); super.new(name, parent); endfunction
                task run_phase(uvm_phase phase);
                  phase.raise_objection(this);
                endtask
              endclass
            endpackage
"""
    results = check_examples(model, "ctx.yaml", _file(rules))
    assert len(results) == 2


def test_the_attribute_form_of_a_tag(model):
    src = (SRC.replace("package lone_agent_pkg;",
                       '(* svcheck_context = "env(lone)" *) package lone_agent_pkg;')
           .replace("interface bus_if;", '(* svcheck_context = "agent(bus)" *) interface bus_if;'))
    r = _run(model, None, src)
    unit = [l for l in _labels(r) if l[2] == "unit" and l[1] in (2, 23)]
    assert unit == [("Package", 23, "unit", "env", "lone", "Declared"),
                    ("Definition", 2, "unit", "agent", "bus", "Declared")]


# applies_to (svcheck-plan P1.5; sv-contexts.md §4.1): selection by the
# context of a node's innermost labelled carrier (Q-CX3).

def _applies(rid, labels):
    return ("    - id: %s\n"
            "      name: r-%s\n"
            "      tier: suspicious\n"
            "      summary: s\n"
            "      message: \"${node.name}\"\n"
            "      applies_to: { unit: %s }\n"
            "      match: { kind: Call }\n" % (rid, rid.lower(), labels))


# An agent package and an env package, each with a run_phase that calls.
UNITS = """\
package a_agent_pkg;
  import uvm_pkg::*;
  class a_agent extends uvm_agent;
    function new(string name, uvm_component parent); super.new(name, parent); endfunction
    task run_phase(uvm_phase phase);
      phase.raise_objection(this);
    endtask
  endclass
endpackage
package e_env_pkg;
  import uvm_pkg::*;
  class e_env extends uvm_env;
    function new(string name, uvm_component parent); super.new(name, parent); endfunction
    task run_phase(uvm_phase phase);
      phase.raise_objection(this);
    endtask
  endclass
endpackage
"""


def test_applies_to_runs_a_rule_only_in_its_contexts(model):
    calls = {}
    for rid, labels in (("ACME010", "agent"), ("ACME011", "env"),
                        ("ACME012", "[agent, env]"), ("ACME013", "test")):
        r = _run(model, _applies(rid, labels), UNITS)
        calls[rid] = [f["line"] for f in r["findings"]]
    assert calls == {"ACME010": [6], "ACME011": [15], "ACME012": [6, 15], "ACME013": []}
    (s,) = [s for s in r["stats"] if s["rule"] == "ACME013"]
    assert s["matches"] == 0


# One package holding an agent class and an env class (sv-contexts.md §3.4):
# the package is {agent, env}; each class keeps its own label.
MIXED = UNITS.replace("endpackage\npackage e_env_pkg;\n  import uvm_pkg::*;\n", "")

CLASS_VOCAB = """\
  define:
    is(c, base): derives_from(c, "uvm_pkg::" + base)
  contexts:
    - label: agent
      axis: unit
      carrier: Class
      when: '!carrier.inLibrary && is(carrier, "uvm_agent")'
    - label: env
      axis: unit
      carrier: Class
      when: '!carrier.inLibrary && is(carrier, "uvm_env")'
    # Packages are carriers too, so they sum their classes' labels.
    - label: agent
      axis: unit
      carrier: Package
      when: 'false'
"""


def test_applies_to_is_decided_by_the_innermost_labelled_carrier(model):
    r = _run(model, _applies("ACME014", "agent"), MIXED, CLASS_VOCAB)
    assert [f["line"] for f in r["findings"]] == [6]
    pkg = [l[3] for l in _labels(r) if l[0] == "Package"]
    assert sorted(pkg) == ["agent", "env"]


def test_applies_to_on_an_axis_nothing_labels_is_not_run(model):
    e = model.mk_lint_engine()
    assert e.load_rules("a.yaml", _file(_applies("ACME015", "agent")
                                        .replace("unit:", "target:")))
    r = e.run(UNITS)
    assert r["ok"], r
    assert [(m["code"], m["message"]) for m in r["markers"]] == [
        ("SVCHK231", "rule ACME015 (r-acme015) not run: no unit has a context on axis "
                     "'target'")]
    assert r["stats"] == []


def test_the_host_names_rules_not_run(model):
    e = model.mk_lint_engine()
    assert e.load_rules("a.yaml", _file(_applies("ACME017", "agent")))
    r = e.run(SRC, {"not_run": {"ACME017": "it needs the compilation, which has 2 errors"}})
    assert [(m["code"], m["fileid"], m["line"], m["message"]) for m in r["markers"]] == [
        ("SVCHK231", -1, -1, "rule ACME017 (r-acme017) not run: it needs the compilation, "
                             "which has 2 errors")]
    assert r["stats"] == []
    r = e.run(SRC, {"not_run": {"NOPE001": "x"}})
    assert r["problems"] == ["not_run: 'NOPE001' is not a loaded rule's ID with a reason"]


@pytest.mark.parametrize("value, message", [
    ("agent", None),
    ("[agent, env]", None),
    ("[]", "'applies_to.unit' must be a label or a non-empty list of labels, not a list"),
    ("[agent, 3]", "'applies_to.unit' must list labels, but this item is an integer"),
])
def test_applies_to_forms(model, value, message):
    e = model.mk_lint_engine()
    e.load_rules("a.yaml", _file(_applies("ACME018", value)))
    errors = [d["message"] for d in e.rule_diagnostics() if d["severity"] == "error"]
    if message is None:
        assert errors == []
        (rule,) = e.rules()
        assert rule["applies_to"] == {"unit": ["agent", "env"] if "env" in value else ["agent"]}
    else:
        assert errors == [message]


def test_applies_to_must_be_a_mapping(model):
    e = model.mk_lint_engine()
    e.load_rules("a.yaml", _file(_applies("ACME019", "agent")
                                 .replace("applies_to: { unit: agent }", "applies_to: agent")))
    assert [d["message"] for d in e.rule_diagnostics() if d["severity"] == "error"] == [
        "'applies_to' must be a mapping of axis to label or list of labels, as "
        "{ unit: agent }, not a string"]
