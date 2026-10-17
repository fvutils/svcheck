"""The corpora of tests/corpora/corpora.yaml (svcheck-plan P2.1), for the scripts.

    import registry
    for c in registry.load(): ...

Each corpus is a dict with the YAML's keys and their defaults filled in, plus
`path` (its file list) and `present` (every directory it needs is there).
"""
import os
import re

import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CORPORA = os.path.join(ROOT, "tests", "corpora")


def load(names=None):
    with open(os.path.join(CORPORA, "corpora.yaml")) as f:
        entries = yaml.safe_load(f)["corpora"]
    out = []
    for e in entries:
        c = {"list": e["name"] + ".f", "needs": [], "precision": True, "exception": None}
        c.update(e)
        c["path"] = os.path.join(CORPORA, c["list"])
        c["present"] = all(os.path.isdir(os.path.join(ROOT, "packages", d)) for d in c["needs"])
        out.append(c)
    if names:
        by = {c["name"]: c for c in out}
        unknown = [n for n in names if n not in by]
        if unknown:
            raise SystemExit("unknown corpus: %s (tests/corpora/corpora.yaml)" % " ".join(unknown))
        out = [by[n] for n in names]
    return out


_EXPORT = re.compile(r"^packages/opentitan-dv/[^/]+/src/")


def source_path(path):
    """A finding's file as a path that is the same in every corpus holding the file:
    an OpenTitan IP's export copies shared cores (the DV library, the prims), so
    `packages/opentitan-dv/<ip>/src/X` is `opentitan/X`; other packages lose
    `packages/`, and the repo's corpora `tests/corpora/`."""
    path = path.replace(os.sep, "/")
    if _EXPORT.match(path):
        return "opentitan/" + _EXPORT.sub("", path)
    for prefix in ("packages/", "tests/corpora/"):
        if path.startswith(prefix):
            return path[len(prefix):]
    return path
