# Sphinx configuration for svcheck's documentation.
#
#   sphinx-build -W -b html docs docs/_build/html
#
# The node reference (docs/model/) is rendered from src/ast/*.yaml at
# docs-build time by pyastbuilder's Sphinx support (astbuilder.sphinx; its
# diagrams need Graphviz's `dot`). Without it, docs/_ext/astb_fallback.py keeps
# the build working and each node reference page says what is missing.
import os
import sys

project = "svcheck"
copyright = "2026, Matthew Ballance and Contributors"
author = "Matthew Ballance"

_here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_here, "_ext"))
# pyastbuilder from packages/ when ivpm put it there, as scripts/gen_sem_model.py does.
_pyastb = os.path.join(os.path.dirname(_here), "packages", "pyastbuilder", "src")
if os.path.isdir(_pyastb):
    sys.path.insert(0, _pyastb)

extensions = []
try:
    import astbuilder.sphinx  # noqa: F401
    import sv_rule_projection  # noqa: F401  (imports astbuilder.projection)
    extensions.append("astbuilder.sphinx")
except ImportError:
    extensions.append("astb_fallback")

astbuilder_models = {
    "sv": {
        "yaml": ["../src/ast/*.yaml"],
        "projections": [
            "sv_rule_projection:RuleProjection",
            {"projection": "cpp", "namespace": "svcheck::sem"},
        ],
        # Page order and titles; one page per src/ast/<group>.yaml (docs/model/<group>.rst).
        "groups": [
            ("core", "The design and its root classes"),
            ("decl", "Declarations"),
            ("type", "Types"),
            ("stmt", "Statements"),
            ("expr", "Expressions"),
            ("unit", "Context units"),
        ],
        "roots": ["Design"],
        "phases": [
            ("lower", "set when the model is lowered from slang"),
            ("classify", "set by the classify stage"),
        ],
        "doc_backticks": "literal",
        "paths": True,
    },
}

exclude_patterns = ["_build", "design", "Thumbs.db", ".DS_Store"]
html_theme = "alabaster"
