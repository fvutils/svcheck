"""The syntax schema: slang's syntax tree as rules see it (svcheck-plan P1.2).

Through the engine's node hooks (tests/engine/hooks.py), which parse SV source
text alone with slang, as `snippet.sv`.
"""
import os
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tests", "engine"))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

from hooks import EngineHooks  # noqa: E402

SYNTAX_TXT = os.path.join(ROOT, "packages", "slang", "scripts", "syntax.txt")


@pytest.fixture(scope="module")
def hooks():
    h = EngineHooks()
    yield h
    h.close()


def _rule(kind, where="true", report=None):
    text = (
        "sv-rules:\n"
        "  rules:\n"
        "    - id: ACME001\n"
        "      name: r\n"
        "      tier: style\n"
        "      summary: s\n"
        "      message: m\n"
        "      match: { kind: %s }\n"
        "      where: \"%s\"\n" % (kind, where))
    if report:
        text += "      report: \"%s\"\n" % report
    return text


def _findings(hooks, source, kind, where="true", report=None):
    r = hooks.lint_run("r.yaml", _rule(kind, where, report), source)
    assert r["loaded"] and r["diagnostics"] == [] and r["failures"] == [], r
    return [(f["kind"], f["line"], f["col"]) for f in r["findings"]]


def _eval(hooks, source, text):
    r = hooks.cel_eval_node(text, source)
    assert r["ok"], r
    return r["result"]["value"]


# ------------------------------------------------------ the exit criterion --

def test_a_class_declaration_rule_fires_on_a_snippet(hooks):
    src = "class good_c;\nendclass\n\nclass Bad;\nendclass\n"
    got = _findings(hooks, src, "ClassDeclaration", "!node.name.text.matches('^[a-z_]+_c$')")
    assert got == [("ClassDeclaration", 4, 7)]        # at the name


# --------------------------------------------------------------- classes --

def test_a_kind_derives_from_its_type(hooks):
    schema = hooks.reflect_schema()
    classes = schema["classes"]
    by_name = {c["name"]: i for i, c in enumerate(classes)}
    for kind, typ in [("ClassDeclaration", "ClassDeclarationSyntax"),
                      ("AddExpression", "BinaryExpressionSyntax"),
                      ("PackageDeclaration", "ModuleDeclarationSyntax")]:
        assert classes[by_name[kind]]["super"] == by_name[typ], kind
        assert classes[by_name[kind]]["fields"] == []
    assert classes[by_name["BinaryExpressionSyntax"]]["super"] == by_name["ExpressionSyntax"]
    assert classes[by_name["ExpressionSyntax"]]["super"] == by_name["SyntaxNode"]


def test_a_type_matches_all_its_kinds(hooks):
    src = "module m; initial begin x = a + b; y = a == b; z = -a; end endmodule\n"
    got = _findings(hooks, src, "BinaryExpressionSyntax")
    assert [k for k, _, _ in got] == ["AssignmentExpression", "AddExpression",
                                      "AssignmentExpression", "EqualityExpression",
                                      "AssignmentExpression"]


@pytest.mark.skipif(not os.path.exists(SYNTAX_TXT), reason="no slang source tree")
def test_every_kind_and_type_is_a_class(hooks):
    import gen_syntax_schema
    types, order, kindmap = gen_syntax_schema.parse(SYNTAX_TXT)
    names = {c["name"] for c in hooks.reflect_schema()["classes"]}
    assert {t + "Syntax" for t in order} <= names
    assert set(kindmap) <= names
    assert len(names) == 1 + 1 + len(order) + len(kindmap)    # "" and SyntaxNode


# ---------------------------------------------------------------- tokens --

def test_tokens(hooks):
    src = "class \\esc+aped ;\nendclass\n"
    assert _eval(hooks, src, "[node.members[0].name.text, node.members[0].name.raw]") == [
        {"type": "string", "value": "esc+aped"}, {"type": "string", "value": "\\esc+aped"}]
    assert _eval(hooks, src, "node.members[0].classKeyword.kind == TokenKind.ClassKeyword")
    assert _eval(hooks, src, "[node.members[0].name.line, node.members[0].name.col]") == [
        {"type": "int", "value": 1}, {"type": "int", "value": 7}]


def test_a_token_the_parser_made_up_is_missing(hooks):
    src = "class A\nendclass\n"                           # no ';'
    assert _eval(hooks, src, "node.members[0].semi.missing") is True
    assert _eval(hooks, src, "node.members[0].name.missing") is False


def test_an_absent_token_is_null(hooks):
    src = "class A; endclass\n"
    assert _eval(hooks, src, "has(node.members[0].virtualOrInterface)") is False
    assert _eval(hooks, src, "node.members[0].virtualOrInterface == null") is True


def test_a_token_list(hooks):
    src = "class A; static local int x; endclass\n"
    assert _eval(hooks, src, "node.members[0].items[0].qualifiers.map(q, q.text)") == [
        {"type": "string", "value": "static"}, {"type": "string", "value": "local"}]


def test_a_separated_list_has_no_separators(hooks):
    src = "module m; int a, b, c; endmodule\n"
    assert _eval(hooks, src,
                 "node.members[0].members[0].declarators.map(d, d.name.text)") == [
        {"type": "string", "value": "a"}, {"type": "string", "value": "b"},
        {"type": "string", "value": "c"}]


def test_a_reserved_word_is_a_field_name(hooks):
    # `package` is a CEL reserved word; after a '.' it is a field.
    src = "module m; import p::*; endmodule\n"
    assert _eval(hooks, src, "node.members[0].members[0].items[0].package.text") == "p"


# ------------------------------------------------------------- locations --

@pytest.mark.parametrize("kind,src,at", [
    ("ClassDeclaration", "  class A; endclass\n", (1, 9)),
    ("ModuleDeclaration", "module top; endmodule\n", (1, 8)),        # header.name
    ("PackageDeclaration", "\npackage p; endpackage\n", (2, 9)),
    ("FunctionDeclaration", "module m;\n  function int f(); endfunction\nendmodule\n",
     (2, 16)),                                                          # prototype.name
    ("ClassMethodDeclaration", "class A;\n  virtual task t(); endtask\nendclass\n",
     (2, 16)),                                                          # declaration...
    ("TypedefDeclaration", "typedef int my_t;\n", (1, 13)),
])
def test_a_declaration_is_reported_at_its_name(hooks, kind, src, at):
    assert _findings(hooks, src, kind) == [(kind,) + at]


def test_other_nodes_are_reported_where_they_start(hooks):
    src = "module m;\n  initial x = 1 +\n    2;\nendmodule\n"
    assert _findings(hooks, src, "AddExpression") == [("AddExpression", 2, 15)]


def test_a_macro_expansion_is_reported_where_the_macro_is_used(hooks):
    src = ("`define MK(n) class n; endclass\n"
           "\n"
           "    `MK(made_by_macro)\n")
    assert _findings(hooks, src, "ClassDeclaration") == [("ClassDeclaration", 3, 5)]


def test_walk_and_ancestors(hooks):
    src = "package p; class A; int x; endclass endpackage\n"
    r = hooks.lint_walk("r.yaml", _rule("DataDeclaration"), src)
    assert r["loaded"], r
    [m] = r["matches"]
    assert m["kind"] == "DataDeclaration"
    assert m["ancestors"] == ["ClassPropertyDeclaration", "ClassDeclaration",
                              "PackageDeclaration", "CompilationUnit"]
