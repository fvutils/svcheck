"""Rule examples run (format §4; svcheck-plan P1.4).

The harness is src/engine/run/Examples.cpp, a port of pssparser's Python
one (python/pssparser/lint/examples.py at 57c6b14); these tests are its
tests (tests/python/lintcore/test_examples.py), on SV. Each example is
compiled by the host (src/host/HookParser.cpp): parsed alone without a
semantic layer, compiled against uvm_pkg with one.
"""
import os

import pytest

from hooks import EngineHooks, check_examples

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
UVM = os.path.join(ROOT, "packages", "uvm-core", "src")


def _rule(examples, where="!node.name.text.matches('^[A-Z]')", rid="ACME001"):
    return ("""\
sv-rules:
  rules:
    - id: %s
      name: r-%s
      tier: style
      summary: s
      message: "class '${node.name.text}'"
      match: { kind: ClassDeclaration }
      where: "%s"
      examples:
""" % (rid, rid.lower(), where)) + examples


def _one(hooks, examples, **kw):
    r = hooks.lint_examples("acme.yaml", _rule(examples, **kw), ["ACME001"])
    assert r["ok"] and r["loaded"], r
    (x,) = r["results"]
    return x


@pytest.fixture(scope="module")
def model():
    h = EngineHooks(semantic="model", uvm=UVM)
    yield h
    h.close()


def test_a_passing_example(engine_hooks):
    r = _one(engine_hooks, """\
        - name: fires
          code: |
            class b;   // expect: ACME001
            endclass
""")
    assert r["ok"] and r["problems"] == []
    assert (r["rule"], r["name"], r["file"], r["line"], r["col"]) == (
        "ACME001", "fires", "acme.yaml", 11, 11)
    assert [(m["code"], m["file"], m["line"], m["col"]) for m in r["markers"]] == [
        ("ACME001", "example.sv", 1, 7)]


def test_missing_and_unexpected(engine_hooks):
    r = _one(engine_hooks, """\
        - name: wrong line
          code: |
            class c;
            endclass
            class D; endclass   // expect: ACME001
""")
    assert r["problems"] == [
        "expected ACME001 at line 3, but it was not reported",
        "ACME001 was reported at 1:7, which the example does not expect"]
    assert r["describe"] == (
        "acme.yaml:11:11: ACME001 example 'wrong line'\n"
        "  expected ACME001 at line 3, but it was not reported\n"
        "  ACME001 was reported at 1:7, which the example does not expect")


def test_a_near_miss_must_report_nothing(engine_hooks):
    r = _one(engine_hooks, """\
        - code: |
            class c; endclass
""")
    assert r["name"] == "example 1"
    assert r["problems"] == ["ACME001 was reported at 1:7, which the example does not expect"]


def test_column_must_match_when_given(engine_hooks):
    r = _one(engine_hooks, """\
        - code: |
            class c; endclass   // expect: ACME001 @ 1:1
""")
    assert r["problems"] == [
        "expected ACME001 at 1:1, but it was not reported",
        "ACME001 was reported at 1:7, which the example does not expect"]


def test_exact_expectations_are_matched_first(engine_hooks):
    # Two findings on one line: the line-only expectation must not take the
    # finding at 1:47 that the exact one needs.
    r = _one(engine_hooks, """\
        - code: |
            class b; /* expect: ACME001 */ endclass class c; /* expect: ACME001 @ 1:47 */ endclass
""")
    assert r["ok"], r["describe"]


def test_a_syntax_error_fails(engine_hooks):
    r = _one(engine_hooks, """\
        - code: |
            class c extends;   // expect: ACME001
            endclass
""")
    (p,) = r["problems"]
    assert p.startswith("the example has errors, so no rule ran: 1:16: "), p
    assert r["markers"] == []


def test_a_semantic_error_fails(model):
    r = model.lint_examples("acme.yaml", _rule("""\
        - code: |
            class c; endclass   // expect: ACME001
            module m; int y = missing; endmodule
"""), ["ACME001"])
    (x,) = r["results"]
    (p,) = x["problems"]
    assert p.startswith("the example has errors, so no rule ran: 2:19: "), p
    assert "missing" in p


def test_a_rule_failure_fails_the_example(engine_hooks):
    r = _one(engine_hooks, """\
        - code: |
            class c; endclass
""", where="node.items[3] != null")
    (p,) = r["problems"]
    assert p.startswith("the rule failed at 1:1: rule ACME001 (r-acme001) failed in its where"), p


def test_several_files_name_the_file(engine_hooks):
    r = _one(engine_hooks, """\
        - files:
            a.sv: |
              class A; endclass
            b.sv: |
              class B; endclass
              class r; endclass   // expect: ACME001
              class q; endclass
""")
    assert r["problems"] == [
        "ACME001 was reported at b.sv:3:7, which the example does not expect"]


def test_options_are_at_their_defaults(engine_hooks):
    rules = _rule("""\
        - code: |
            class Ab; endclass   // expect: ACME001
""", where="node.name.text.size() < options.min")
    rules = rules.replace("      examples:\n",
                          "      options:\n"
                          "        min: { type: int, default: 3, help: h }\n"
                          "      examples:\n")
    check_examples(engine_hooks, "acme.yaml", rules)


def test_only_its_own_rule_runs(engine_hooks):
    two = _rule("""\
        - code: |
            class b; endclass   // expect: ACME001
""") + _rule("", rid="ACME002").split("  rules:\n", 1)[1].rstrip() + " []\n"
    r = engine_hooks.lint_examples("acme.yaml", two, ["ACME001", "ACME002"])
    assert [x["rule"] for x in r["results"]] == ["ACME001"]
    assert r["results"][0]["ok"]
    assert {m["code"] for m in r["results"][0]["markers"]} == {"ACME001"}


def test_check_examples_raises_with_every_failure(engine_hooks):
    with pytest.raises(AssertionError) as exc:
        check_examples(engine_hooks, "acme.yaml", _rule("""\
        - name: one
          code: |
            class c; endclass
        - name: two
          code: |
            class d; endclass
"""))
    assert "example 'one'" in str(exc.value) and "example 'two'" in str(exc.value)


def test_a_uvm_example_compiles_against_uvm_pkg(model):
    if not os.path.isfile(os.path.join(UVM, "uvm_pkg.sv")):
        pytest.skip("UVM is not in packages/ (ivpm update)")
    rules = """\
sv-rules:
  rules:
    - id: ACME001
      name: r
      tier: style
      summary: s
      message: m
      match: { kind: Class }
      where: "!node.inLibrary && derives_from(node, 'uvm_pkg::uvm_component')"
      examples:
        - code: |
            import uvm_pkg::*;
            class c extends uvm_component;   // expect: ACME001
              function new(string name, uvm_component parent); super.new(name, parent); endfunction
            endclass
            class d; endclass
"""
    (r,) = check_examples(model, "acme.yaml", rules)
    assert [(m["file"], m["line"]) for m in r["markers"]] == [("example.sv", 2)]

