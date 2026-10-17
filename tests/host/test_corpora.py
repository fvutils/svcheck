"""The corpora of tests/corpora/corpora.yaml (svcheck-plan P2.1): each compiles
with no slang error, or is listed with the exception and still has errors (so a
fix shows), and the precision reviews in tests/precision/ match what the rules
report (`scripts/precision.py --check`). A corpus whose packages are absent is
skipped (tests/corpora/README.md).
"""
import json
import os
import subprocess
import sys

import pytest

from svcheck import native_path

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "scripts", "corpora"))

import registry  # noqa: E402

CORPORA = registry.load()


@pytest.mark.parametrize("corpus", CORPORA, ids=[c["name"] for c in CORPORA])
def test_corpus_compiles(corpus):
    if not corpus["present"]:
        pytest.skip("needs packages/%s (tests/corpora/README.md)" % ", ".join(corpus["needs"]))
    r = subprocess.run([native_path(), "--rules", os.path.join(ROOT, "rules", "spike", "verible.yaml"),
                        "-F", corpus["path"], "--format", "json"],
                       capture_output=True, text=True, timeout=300, cwd=ROOT)
    out = json.loads(r.stdout)
    if corpus["exception"]:
        assert out["compilation"]["errors"] > 0, (
            "%s compiles now: remove its exception from corpora.yaml" % corpus["name"])
    else:
        assert out["compilation"]["errors"] == 0, r.stderr
        assert out["compilation"]["unparsed"] == []


def test_precision_reviews_are_current():
    present = [c for c in CORPORA if c["present"] and c["precision"]]
    if not present:
        pytest.skip("no corpus present (tests/corpora/README.md)")
    r = subprocess.run([sys.executable, os.path.join(ROOT, "scripts", "precision.py"), "--check",
                        "--svcheck", native_path()],
                       capture_output=True, text=True, timeout=900, cwd=ROOT)
    assert r.returncode == 0, ("a finding is new or gone: run scripts/precision.py --update "
                               "and review it\n" + r.stdout + r.stderr)
