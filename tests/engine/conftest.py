"""Fixtures for the engine tests (copied from pssparser's tests/python/lintcore).

pssparser's tests reach the hooks as `factory._lintTestHooks()`; `factory`
here is a stand-in that gives the one EngineHooks server of the session.
"""
import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from hooks import EngineHooks  # noqa: E402


class _Factory:
    def __init__(self, hooks):
        self._hooks = hooks

    def _lintTestHooks(self):
        return self._hooks


@pytest.fixture(scope="session")
def engine_hooks():
    h = EngineHooks()
    yield h
    h.close()


@pytest.fixture(scope="session")
def factory(engine_hooks):
    return _Factory(engine_hooks)


def _needs_schema():
    """needs_schema.txt: node id -> the reason it is held back."""
    held = {}
    reason = ""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "needs_schema.txt")
    with open(path, encoding="utf-8") as fp:
        for line in fp:
            line = line.strip()
            if line.startswith("## "):
                reason = line[3:]
            elif line and not line.startswith("#"):
                held[line] = reason
    return held


def pytest_collection_modifyitems(config, items):
    held = _needs_schema()
    for item in items:
        nid = item.nodeid.replace(os.sep, "/")
        if nid in held:
            item.add_marker(pytest.mark.skip(
                reason="needs the SV schema (svcheck-plan P1.2): " + held[nid]))
