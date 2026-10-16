"""
The lint engine's CEL evaluator (lint-core-plan.md C3.1).

Every value kind (null, bool, int, double, string, list, map, optional, node,
struct, type, error) and CEL's semantics over them: commutative error
absorption in `&&`/`||`/`?:`, heterogeneous numeric equality, int64 overflow
as an error, comprehensions with early exit, the step budget, optional
chaining, node identity, and D-C6 (navigating through an unset node field is
an error value).

Reached through the private hooks `cel_eval(text, max_steps, bindings)` and,
for node values, `cel_eval_node(text, root, ...)`, which binds `node`.
"""
import pytest


@pytest.fixture(scope="module")
def hooks(factory):
    return factory._lintTestHooks()


# ------------------------------------------------------------ CEL eval ----

def _val(hooks, text):
    r = hooks.cel_eval(text)
    assert r["ok"], r
    return r["result"]["type"], r["result"]["value"]


@pytest.mark.parametrize("text,expected", [
    ("false && (1/0 == 1)", ("bool", False)),
    ("(1/0 == 1) && false", ("bool", False)),
    ("true || {}['x']", ("bool", True)),
    ("{}['x'] || true", ("bool", True)),
    ("1 == 1.0", ("bool", True)),
    ("1 < 1.5", ("bool", True)),
    ("[1, 2].map(x, x * 2)", ("list", [{"type": "int", "value": 2}, {"type": "int", "value": 4}])),
    ("[1, 2, 3].exists_one(x, x > 2)", ("bool", True)),
    ("[1, 2, 3].filter(x, x > 1).size()", ("int", 2)),
    ("size('héllo')", ("int", 5)),
    ("'abc'.matches('^a.c$')", ("bool", True)),
    ("1.0 / 0.0", ("double", "Infinity")),
    ("2.0", ("double", 2.0)),
])
def test_cel_eval_values(hooks, text, expected):
    assert _val(hooks, text) == expected


@pytest.mark.parametrize("text,msg", [
    ("9223372036854775807 + 1", "overflow"),
    ("-9223372036854775807 - 2", "overflow"),
    ("1 / 0", "division by zero"),
    ("[1][1]", "out of range"),
    ("[1][-1]", "out of range"),
    ("{'a': 1/0}", "division by zero"),
    ("1 + 1.0", "no matching overload"),
    ("x", "undeclared reference"),
])
def test_cel_eval_errors(hooks, text, msg):
    t, v = _val(hooks, text)
    assert t == "error"
    assert msg in v


def test_cel_early_exit_and_budget(hooks):
    # exists() stops at the first true element: the step count does not grow
    # with the list.
    short = hooks.cel_eval("[1, 2].exists(x, x == 1)")["steps"]
    long_ = hooks.cel_eval("[1, " + ", ".join(["2"] * 200) + "].exists(x, x == 1)")["steps"]
    assert long_ - short < 450   # the literal list's elements, not the loop
    r = hooks.cel_eval("[" + ", ".join(["1"] * 100) + "].map(x, x + 1)", 50)
    assert r["budget_exceeded"]
    assert "budget" in r["result"]["value"]


# ------------------------------------------------- error absorption -------

# Each operand is one of three outcomes; `E` is an error (1/0 == 1).
_OPERANDS = {"T": "true", "F": "false", "E": "(1/0 == 1)"}
_AND = {"TT": True, "TF": False, "TE": "E", "FT": False, "FF": False, "FE": False,
        "ET": "E", "EF": False, "EE": "E"}
_OR = {"TT": True, "TF": True, "TE": True, "FT": True, "FF": False, "FE": "E",
       "ET": True, "EF": "E", "EE": "E"}


@pytest.mark.parametrize("op,table", [("&&", _AND), ("||", _OR)])
def test_cel_error_absorption_truth_table(hooks, op, table):
    for pair, want in table.items():
        text = "%s %s %s" % (_OPERANDS[pair[0]], op, _OPERANDS[pair[1]])
        t, v = _val(hooks, text)
        if want == "E":
            assert (t, "division by zero" in v) == ("error", True), text
        else:
            assert (t, v) == ("bool", want), text


@pytest.mark.parametrize("text,expected", [
    ("true ? 1 : 1/0", ("int", 1)),
    ("false ? 1/0 : 2", ("int", 2)),
    ("!(1/0 == 1) || true", ("bool", True)),
    ("[1/0 == 1, false].exists(x, x == false)", None),   # list literal: error wins
    ("[1, 2].all(x, x / 0 == 1 || x > 0)", ("bool", True)),
    ("[0, 1].exists(x, 1 / x == 1)", ("bool", True)),     # error on 0, absorbed by 1
    ("[0, 2].exists(x, 1 / x == 1)", ("error", "division by zero")),
    ("[0, 1].all(x, 1 / x == 5)", ("bool", False)),
])
def test_cel_error_absorption_elsewhere(hooks, text, expected):
    t, v = _val(hooks, text)
    if expected is None:
        assert t == "error"
    elif expected[0] == "error":
        assert t == "error" and expected[1] in v
    else:
        assert (t, v) == expected


def test_cel_conditional_error_condition(hooks):
    t, v = _val(hooks, "(1/0 == 1) ? 1 : 2")
    assert t == "error" and "division by zero" in v
    t, v = _val(hooks, "1 ? 1 : 2")
    assert t == "error" and "no matching overload" in v


# --------------------------------------------------------- overflow -------

@pytest.mark.parametrize("text", [
    "9223372036854775807 + 1", "-9223372036854775808 - 1", "-(-9223372036854775808)",
    "4611686018427387904 * 2", "-9223372036854775808 * -1", "-9223372036854775808 / -1",
    "-9223372036854775808 % -1", "int(9.3e18)", "int(-9.3e18)",
])
def test_cel_overflow(hooks, text):
    t, v = _val(hooks, text)
    assert t == "error" and ("overflow" in v or "out of range" in v), text


@pytest.mark.parametrize("text,expected", [
    ("9223372036854775807 + 0", 9223372036854775807),
    ("-9223372036854775807 - 1", -9223372036854775808),
    ("4611686018427387903 * 2", 9223372036854775806),
    ("-9223372036854775808 / 1", -9223372036854775808),
    ("-7 / 2", -3), ("-7 % 2", -1),
])
def test_cel_no_overflow_at_the_edge(hooks, text, expected):
    assert _val(hooks, text) == ("int", expected)


# ------------------------------------------- heterogeneous equality -------

@pytest.mark.parametrize("text,expected", [
    ("1 == 1.0", True), ("1 != 1.0", False), ("1 == 1.5", False), ("2 > 1.5", True),
    ("1 in [1.0, 2.0]", True), ("1.0 in [1, 2]", True), ("2.5 in [2]", False),
    ("{1: 'a'}[1.0] == 'a'", True), ("1.0 in {1: 'a'}", True),
    ("[1, 2] == [1.0, 2.0]", True), ("{'a': 1} == {'a': 1.0}", True),
    # The int converts to double, lossily, as cel-spec's conformance tests
    # require: 2^63 - 1 rounds to 2^63.
    ("9223372036854775807 == 9223372036854775808.0", True),
    ("9223372036854775807 < 9223372036854775808.0", False),
    ("9007199254740993 == 9007199254740992.0", True),
    ("0.0 / 0.0 == 0.0 / 0.0", False), ("1 < 0.0 / 0.0", False), ("1 > 0.0 / 0.0", False),
    ("1 == 'a'", False), ("null == 0", False), ("[1] == ['1']", False),
    ("-0.0 == 0", True),
])
def test_cel_numeric_equality(hooks, text, expected):
    assert _val(hooks, text) == ("bool", expected), text


# --------------------------------------------- early exit and budget ------

def test_cel_all_stops_at_first_false(hooks):
    short = hooks.cel_eval("[0, 1].all(x, x == 1)")["steps"]
    long_ = hooks.cel_eval("[0, " + ", ".join(["1"] * 200) + "].all(x, x == 1)")["steps"]
    # The literal costs one step per element; the loop does not run past [0].
    assert long_ - short <= 201


def test_cel_budget_reported_once_exceeded(hooks):
    r = hooks.cel_eval("[1, 2, 3].map(x, [1, 2, 3].map(y, x * y))", 20)
    assert r["budget_exceeded"] and r["result"]["type"] == "error"
    assert "20 steps" in r["result"]["value"]
    r = hooks.cel_eval("[1, 2, 3].map(x, [1, 2, 3].map(y, x * y))", 1000)
    assert not r["budget_exceeded"] and r["result"]["type"] == "list"
    assert r["steps"] <= 1000


def test_cel_budget_absorbed_by_nothing(hooks):
    # The budget is not a CEL error: `||` cannot absorb it.
    r = hooks.cel_eval("true || [1, 2, 3].map(x, x).size() > 0", 3)
    assert not r["budget_exceeded"]   # short-circuits before the loop
    r = hooks.cel_eval("[1, 2, 3].map(x, x).size() > 0 || true", 3)
    assert r["budget_exceeded"] and r["result"]["type"] == "error"


# -------------------------------------------------------- bindings --------

def test_cel_bindings(hooks):
    r = hooks.cel_eval("x + y.size() + m.k[1]", bindings={"x": 1, "y": "ab", "m": {"k": [5, 6]}})
    assert r["result"] == {"type": "int", "value": 9}
    # A variable shadows a type name.
    assert hooks.cel_eval("int + 1", bindings={"int": 2})["result"]["value"] == 3
    assert hooks.cel_eval("[1].map(x, x + x)", bindings={"x": 10})["result"]["value"] == \
        [{"type": "int", "value": 2}]


# ----------------------------------------------------------- types --------

@pytest.mark.parametrize("text,expected", [
    ("type(1) == int", ("bool", True)), ("type(1.0) == double", ("bool", True)),
    ("type('') == string", ("bool", True)), ("type(true) == bool", ("bool", True)),
    ("type(null) == null_type", ("bool", True)), ("type([]) == list", ("bool", True)),
    ("type({}) == map", ("bool", True)), ("type(int) == type", ("bool", True)),
    ("type(type)", ("type", "type")), ("type(optional.none()) == optional_type", ("bool", True)),
    ("type(1) == double", ("bool", False)), ("int == int", ("bool", True)),
    ("BinaryExpressionSyntax", ("type", "BinaryExpressionSyntax")),
    ("AddExpression", ("type", "AddExpression")), ("Token", ("type", "Token")),
    # The first of slang's TokenKinds, whose values have stayed put.
    ("TokenKind.Unknown", ("int", 0)), ("TokenKind.EndOfFile", ("int", 1)),
    ("TokenKind.Identifier", ("int", 2)), ("TokenKind.SystemIdentifier", ("int", 3)),
])
def test_cel_types_and_enum_constants(hooks, text, expected):
    assert _val(hooks, text) == expected


@pytest.mark.parametrize("text,msg", [
    ("TokenKind.Nope", "type 'TokenKind' has no member 'Nope'"),
    ("BinaryExpressionSyntax.left", "type 'BinaryExpressionSyntax' has no member 'left'"),
    ("NoSuchClass", "undeclared reference to 'NoSuchClass'"),
    ("int < int", "no matching overload"),
])
def test_cel_type_errors(hooks, text, msg):
    t, v = _val(hooks, text)
    assert t == "error" and msg in v


def test_cel_type_names_do_not_collide(hooks):
    # Class, struct and enum names share one namespace in expressions.
    s = hooks.reflect_schema()
    classes = {c["name"] for c in s["classes"] if c}
    enums = {e["name"] for e in s["enums"]}
    structs = {x["name"] for x in s["structs"]}
    builtin = {"bool", "int", "double", "string", "list", "map", "null_type", "type",
               "optional_type"}
    assert not (classes & enums) and not (classes & structs) and not (enums & structs)
    assert not ((classes | enums | structs) & builtin)


# ------------------------------------------------------- optionals --------

@pytest.mark.parametrize("text,expected", [
    ("optional.of(1).hasValue()", ("bool", True)),
    ("optional.none().hasValue()", ("bool", False)),
    ("optional.of(null).hasValue()", ("bool", True)),
    ("optional.ofNonZeroValue(0).hasValue()", ("bool", False)),
    ("optional.ofNonZeroValue('').hasValue()", ("bool", False)),
    ("optional.ofNonZeroValue([1]).hasValue()", ("bool", True)),
    ("optional.of(1).value()", ("int", 1)),
    ("optional.none().orValue(2)", ("int", 2)),
    ("optional.of(1).orValue(1/0)", ("int", 1)),          # argument not evaluated
    ("optional.none().or(optional.of(3)).value()", ("int", 3)),
    ("optional.of(1).or(optional.of(1/0)).value()", ("int", 1)),
    ("{'a': 1}.?a.value()", ("int", 1)),
    ("{'a': 1}.?b.hasValue()", ("bool", False)),
    ("{}.?a.b.c.hasValue()", ("bool", False)),            # none chains
    ("optional.of({'a': {'b': 2}}).a.b.value()", ("int", 2)),
    ("optional.of({'a': {}}).a.b.hasValue()", ("bool", False)),
    ("[1, 2][?1].value()", ("int", 2)),
    ("[1, 2][?2].hasValue()", ("bool", False)),
    ("[1, 2][?-1].hasValue()", ("bool", False)),
    ("{'k': 1}[?'k'].value()", ("int", 1)),
    ("optional.of([1])[0].value()", ("int", 1)),
    ("optional.of([1])[5].hasValue()", ("bool", False)),
    ("optional.none()[0].hasValue()", ("bool", False)),
    ("has({'a': optional.none()}.a)", ("bool", True)),
    ("has({'a': optional.none()}.a.b)", ("bool", False)),
    ("has({}.?a.b)", ("bool", False)),
    ("has({'a': {'b': 1}}.?a.b)", ("bool", True)),
    ("optional.none() == optional.none()", ("bool", True)),
    ("optional.of(1) == optional.of(1.0)", ("bool", True)),
    ("optional.of(1) == optional.none()", ("bool", False)),
    ("[?optional.none(), ?optional.of(1), 2]",
     ("list", [{"type": "int", "value": 1}, {"type": "int", "value": 2}])),
    ("{?'a': optional.none(), ?'b': optional.of(1)}.size()", ("int", 1)),
    ("{?'a': optional.of(1)}.a", ("int", 1)),
])
def test_cel_optionals(hooks, text, expected):
    assert _val(hooks, text) == expected, text


@pytest.mark.parametrize("text,msg", [
    ("optional.none().value()", "optional.none() dereference"),
    ("{'a': null}.?a.b", "no such key"),
    ("[?1]", "optional list element needs an optional value"),
    ("{?'a': 1}", "optional map entry needs an optional value"),
    ("optional.of(1, 2)", "optional.of"),
    ("optional.none().or(1)", "no matching overload"),
    ("optional.of(1/0)", "division by zero"),
])
def test_cel_optional_errors(hooks, text, msg):
    t, v = _val(hooks, text)
    assert t == "error" and msg in v, (text, v)


def test_cel_optional_namespace_can_be_shadowed(hooks):
    r = hooks.cel_eval("optional.of", bindings={"optional": {"of": 5}})
    assert r["result"] == {"type": "int", "value": 5}


# ----------------------------------------------------------- nodes --------

# Parsed alone (snippet.sv), so `node` is its CompilationUnit.
_MODEL = (
    "package top;\n"
    "  class A;\n"
    "    rand int a;\n"
    "    constraint c { a > 0; }\n"
    "  endclass\n"
    "  class B extends A; endclass\n"
    "endpackage\n")

# Bound with cel.bind so the expressions below stay readable.
_PRELUDE = (
    "cel.bind(top, node.members.filter(c, type(c) == PackageDeclaration)[0],"
    " cel.bind(A, top.members.filter(c, type(c) == ClassDeclaration && c.name.text == 'A')[0],"
    " cel.bind(B, top.members.filter(c, type(c) == ClassDeclaration && c.name.text == 'B')[0],"
    " cel.bind(fa, A.items.filter(c, type(c) == ClassPropertyDeclaration)[0],"
    " cel.bind(ex, A.items.filter(c, type(c) == ConstraintDeclaration)[0].block.items"
    ".filter(s, type(s) == ExpressionConstraint).map(s, s.expr)"
    ".filter(e, type(e) == GreaterThanExpression)[0],"
    " %s)))))")


@pytest.fixture(scope="module")
def unit():
    return _MODEL


def _node(hooks, unit, text, max_steps=0):
    r = hooks.cel_eval_node(_PRELUDE % text, unit, max_steps)
    assert r["ok"], r
    return r["result"]["type"], r["result"]["value"]


@pytest.mark.parametrize("text,expected", [
    ("type(node)", ("type", "CompilationUnit")),
    ("type(A) == ClassDeclaration", ("bool", True)),
    ("A.name.text", ("string", "A")),
    ("A.classKeyword.line", ("int", 2)),
    ("A.classKeyword.col", ("int", 3)),
    ("type(A.name)", ("type", "Token")),
    ("A.name == A.name", ("bool", True)),
    ("A.name == B.name", ("bool", False)),
    ("A.attributes.size()", ("int", 0)),                # inherited (MemberSyntax)
    ("[fa.declaration].filter(d, type(d) == DataDeclaration)[0].declarators[0].name.text",
     ("string", "a")),
    ("fa.qualifiers[0].kind == TokenKind.RandKeyword", ("bool", True)),
    ("type(ex) == GreaterThanExpression", ("bool", True)),
    ("ex.operatorToken.kind == TokenKind.GreaterThan", ("bool", True)),
    ("type(ex.left) == IdentifierName && type(ex.right) == IntegerLiteralExpression",
     ("bool", True)),
    ("A.items.size()", ("int", 2)),
    ("A.items.exists(c, type(c) == ConstraintDeclaration)", ("bool", True)),
    ("fa.qualifiers.size()", ("int", 1)),
    ("A == A", ("bool", True)),                         # identity
    ("A == B", ("bool", False)),
    ("A in top.members", ("bool", True)),
    ("A in B.items", ("bool", False)),
    ("A != null", ("bool", True)),
    ("type(B.extendsClause) == ExtendsClause", ("bool", True)),
])
def test_cel_node_values(hooks, unit, text, expected):
    assert _node(hooks, unit, text) == expected, text


@pytest.mark.parametrize("text,expected", [
    # proto3 presence: set node, non-empty list, non-zero scalar; a token
    # the source has.
    ("has(B.extendsClause)", True), ("has(A.extendsClause)", False),
    ("has(A.items)", True), ("has(A.attributes)", False),
    ("has([fa.declaration].filter(d, type(d) == DataDeclaration)[0].declarators[0]"
     ".initializer)", False),
    ("has(A.name)", True), ("has(A.virtualOrInterface)", False),
    ("has(fa.qualifiers)", True), ("has(A.classKeyword)", True),
    ("has(A.classKeyword.line)", True),
    ("has(ex.operatorToken.kind)", True),               # GreaterThan != 0
])
def test_cel_node_has(hooks, unit, text, expected):
    assert _node(hooks, unit, text) == ("bool", expected), text


def test_cel_unset_field_d_c6(hooks, unit):
    # An unset node field reads as null ...
    assert _node(hooks, unit, "A.extendsClause") == ("null_type", None)
    assert _node(hooks, unit, "A.extendsClause == null") == ("bool", True)
    # ... and navigating through it is an error value naming both fields.
    t, v = _node(hooks, unit, "A.extendsClause.baseName")
    assert (t, v) == ("error", "field 'baseName' of unset 'extendsClause'")
    t, v = _node(hooks, unit, "has(A.extendsClause.baseName)")
    assert (t, v) == ("error", "field 'baseName' of unset 'extendsClause'")
    # && and || absorb it, as CEL absorbs any error ...
    assert _node(hooks, unit, "has(A.extendsClause) && A.extendsClause.baseName == null") == \
        ("bool", False)
    assert _node(hooks, unit, "A.extendsClause.baseName == null && false") == ("bool", False)
    assert _node(hooks, unit, "!has(A.extendsClause) || A.extendsClause.baseName == null") == \
        ("bool", True)
    # ... and `.?` turns it into none.
    assert _node(hooks, unit, "A.?extendsClause.hasValue()") == ("bool", False)
    assert _node(hooks, unit, "A.?extendsClause.baseName.hasValue()") == ("bool", False)
    assert _node(hooks, unit, "B.?extendsClause.hasValue()") == ("bool", True)
    assert _node(hooks, unit, "type(B.?extendsClause.value()) == ExtendsClause") == \
        ("bool", True)


@pytest.mark.parametrize("text,msg", [
    ("A.nope", "type 'ClassDeclaration' has no field 'nope'"),
    ("has(A.nope)", "type 'ClassDeclaration' has no field 'nope'"),
    ("A.?nope", "type 'ClassDeclaration' has no field 'nope'"),
    ("A.name.nope", "type 'Token' has no field 'nope'"),
    ("A + 1", "no matching overload for '_+_' applied to (ClassDeclaration, int)"),
    ("A < B", "no matching overload"),
    ("A[0]", "no matching overload"),
])
def test_cel_node_errors(hooks, unit, text, msg):
    t, v = _node(hooks, unit, text)
    assert t == "error" and msg in v, (text, v)


def test_cel_node_list_reads_charge_steps(hooks, unit):
    # Reading a list field costs a step per element, so a rule cannot walk a
    # big list for free.
    base = _node_steps(hooks, unit, "A")
    one = _node_steps(hooks, unit, "A.items.size()")
    two = _node_steps(hooks, unit, "A.items.size() + A.items.size()")
    assert two - one >= 3 + 2 and one - base >= 3


def _node_steps(hooks, unit, text):
    return hooks.cel_eval_node(_PRELUDE % text, unit)["steps"]


def test_cel_node_locations_are_where_the_macro_is_used(hooks):
    """A node a macro makes is where the macro is used, not in the macro's
    body (S-3)."""
    src = "`define C(n) class n; endclass\n\n  `C(foo)\n"
    r = hooks.cel_eval_node(
        "node.members.map(m, [m.name.text, m.name.line, m.name.col])", src)
    assert r["result"]["value"] == [{"type": "list", "value": [
        {"type": "string", "value": "foo"}, {"type": "int", "value": 3},
        {"type": "int", "value": 3}]}]


def test_cel_node_bad_source_still_parses(hooks):
    """slang recovers from errors; the tree it makes is still a tree."""
    r = hooks.cel_eval_node("type(node.members[0])", "class A; int endclass\n")
    assert r["ok"] and r["result"] == {"type": "type", "value": "ClassDeclaration"}
