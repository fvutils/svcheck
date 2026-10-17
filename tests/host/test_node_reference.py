"""The documentation for rule authors (svcheck-plan P2.2): the node reference
(docs/model/, generated from src/ast/*.yaml by pyastbuilder's Sphinx support)
and the function reference (docs/rules/functions.rst, generated from the
function registry).

- The node reference's "Rule" column is derived from the YAML by
  docs/_ext/sv_rule_projection.py, so the docs build without svcheck. Here it
  is compared, for every field of every model class, with what the built
  checker reports for ``type(d) == Class`` ... ``d.field``. (After pssparser's
  tests/python/lintcore/test_rule_projection.py.)
- docs/rules/functions.rst must match the registry: if this fails, rebuild,
  run ``scripts/gen_rule_functions.py`` and commit the result.
- The docs build with Sphinx, warnings as errors.
"""
import glob
import os
import subprocess
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tests", "engine"))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
sys.path.insert(0, os.path.join(ROOT, "docs", "_ext"))
_PYASTB = os.path.join(ROOT, "packages", "pyastbuilder", "src")
if os.path.isdir(_PYASTB):
    sys.path.insert(0, _PYASTB)

from hooks import EngineHooks  # noqa: E402


@pytest.fixture(scope="module")
def model():
    h = EngineHooks(semantic="model")
    yield h
    h.close()


def test_rule_projection_matches_checker(model):
    # The linker needs pyastbuilder's own dependencies (toposort), which a
    # build-only environment (the CI wheel build) may not have.
    pytest.importorskip("astbuilder.linker")
    pytest.importorskip("astbuilder.projection")
    from astbuilder.ast import Ast
    from astbuilder.linker import Linker
    from astbuilder.parser import Parser
    from sv_rule_projection import RuleProjection

    ast = Ast()
    for f in sorted(glob.glob(os.path.join(ROOT, "src", "ast", "*.yaml"))):
        with open(f) as fp:
            Parser(ast, f).parse(fp)
    Linker().link(ast)
    proj = RuleProjection(ast)
    got = {(c.name, d.name): proj.field_type(c, d) for c in ast.classes for d in c.data}

    want = {}
    for (cls, field) in got:
        expr = "descendants(node).filter(d, type(d) == %s).map(d, d.%s)" % (cls, field)
        r = model.cel_check_node({"expr": expr}, "module m; endmodule\n")
        t = None
        if not r["errors"]:
            t = r["types"]["expr"]
            assert t.startswith("list(") and t.endswith(")"), t
            t = t[len("list("):-1]
        want[(cls, field)] = t
    diff = {k: (got[k], want[k]) for k in got if got[k] != want[k]}
    assert diff == {}, diff


def test_function_reference_is_up_to_date(model):
    from gen_rule_functions import OUTPUT_PATH, render
    reg = model.cel_functions()
    assert OUTPUT_PATH.read_text() == render(reg["functions"], reg["records"]), (
        "docs/rules/functions.rst is stale: run scripts/gen_rule_functions.py "
        "and commit the result")


def test_docs_build(tmp_path):
    pytest.importorskip("sphinx")
    pytest.importorskip("astbuilder.sphinx")
    r = subprocess.run(
        [sys.executable, "-m", "sphinx", "-W", "--keep-going", "-q", "-b", "html",
         os.path.join(ROOT, "docs"), str(tmp_path / "html")],
        capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    for page in ("index", "rules/authoring", "rules/functions", "model/index", "model/stmt"):
        assert (tmp_path / "html" / (page + ".html")).is_file(), page
