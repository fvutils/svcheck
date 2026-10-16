"""What the fork changed in pssparser's engine (svcheck-plan P1.1).

- The root key is `sv-rules`; `pss-rules` is not a rule file.
- `SVCHK` is the reserved namespace: a site file may not use it, with or
  without a team qualifier, and a built-in file uses nothing else.
- The engine's own codes are SVCHK2xx.

test_rule_loader's fixture test checks the namespace fixtures
(tests/data/sv-rules/ids-*.yaml) whole; here only their SVCHK217
expectations are, so the policy has a test of its own.
"""
import os
import re

import pytest

FIXTURES = os.path.join(os.path.dirname(__file__), "..", "data", "sv-rules")
EXPECT = re.compile(r"^# expect: (SVCHK217) (\w+) @ (\d+):(\d+) (.*)$")


@pytest.fixture(scope="module")
def hooks(factory):
    return factory._lintTestHooks()


def test_sv_rules_is_the_root_key(hooks):
    r = hooks.rule_load("r.yaml", "sv-rules:\n  rules: []\n")
    assert (r["loaded"], r["diagnostics"]) == (True, [])


def test_pss_rules_is_not_a_rule_file(hooks):
    r = hooks.rule_load("r.yaml", "pss-rules:\n  rules: []\n")
    assert not r["loaded"]
    assert [d["code"] for d in r["diagnostics"]] == ["SVCHK210"]
    assert "no top-level 'sv-rules' key" in r["diagnostics"][0]["message"]


@pytest.mark.parametrize("name,builtin", [
    ("ids-reserved.yaml", False),
    ("ids-builtin.yaml", True),
])
def test_namespace_policy(hooks, name, builtin):
    with open(os.path.join(FIXTURES, name), encoding="utf-8") as fp:
        text = fp.read()
    want = []
    for line in text.splitlines():
        m = EXPECT.match(line)
        if m:
            want.append((m.group(1), m.group(2), int(m.group(3)), int(m.group(4)),
                         m.group(5)))
    assert want
    r = hooks.rule_load(name, text, builtin)
    got = [(d["code"], d["severity"], d["line"], d["col"], d["message"])
           for d in r["diagnostics"] if d["code"] == "SVCHK217"]
    assert got == want
