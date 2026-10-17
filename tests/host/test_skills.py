"""The agent skills in skills/, shipped in the wheel (python/svcheck/skills.py).

An agent follows a skill literally, so a wrong flag or a rule that no longer
loads is a defect. After pssparser's tests/python/skills/test_skills.py:

- every skill's frontmatter, and every reference linked from its SKILL.md;
- the ``agent.skills`` entry points, one per shipped skill;
- every ``svcheck`` flag and every ``SVCHK###`` ID the text names exists;
- every fenced ``yaml`` block that is a rule file passes ``--check-rules``
  (a block tagged ``no-check`` is a fragment and is skipped);
- the generated references match scripts/gen_skill_refs.py's output.
"""
import importlib.metadata
import os
import re
import subprocess
import sys

import pytest

from svcheck import native_path, skills

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SKILLS = os.path.join(ROOT, "skills")
UVM = os.path.join(ROOT, "packages", "uvm-core", "src")
sys.path.insert(0, os.path.join(ROOT, "tests", "engine"))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

ALL = sorted(d for d in os.listdir(SKILLS) if os.path.isfile(os.path.join(SKILLS, d, "SKILL.md")))
FENCE = re.compile(r"^```([^\n`]*)\n(.*?)^```\s*$", re.S | re.M)


def md_files(skill=None):
    out = []
    for d, _, files in os.walk(os.path.join(SKILLS, skill) if skill else SKILLS):
        out += [os.path.join(d, f) for f in files if f.endswith(".md")]
    return sorted(out)


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def frontmatter(path):
    m = re.match(r"---\n(.*?)\n---\n", read(path), re.S)
    assert m, "%s: no frontmatter" % path
    out = {}
    for line in m.group(1).splitlines():
        key, sep, value = line.partition(":")
        assert sep, "%s: not 'key: value': %r" % (path, line)
        out[key.strip()] = value.strip()
    return out


def test_shipped_skills_are_the_repositorys():
    assert sorted(skills.SHIPPED) == ALL


@pytest.mark.parametrize("name", ALL)
def test_frontmatter(name):
    fm = frontmatter(os.path.join(SKILLS, name, "SKILL.md"))
    assert fm.get("name") == name
    desc = fm.get("description", "")
    assert desc and len(desc) <= 1024
    # A YAML plain scalar cannot hold ': '.
    assert ": " not in desc


@pytest.mark.parametrize("name", ALL)
def test_references_are_linked(name):
    # An agent reads a reference only when SKILL.md points to it.
    text = read(os.path.join(SKILLS, name, "SKILL.md"))
    refs = os.path.join(SKILLS, name, "references")
    for f in sorted(os.listdir(refs)) if os.path.isdir(refs) else []:
        assert "references/%s" % f in text, "%s/SKILL.md never names references/%s" % (name, f)


def test_entry_points():
    dist = importlib.metadata.distribution("svcheck")
    eps = {e.name: e for e in dist.entry_points if e.group == "agent.skills"}
    assert sorted(eps) == sorted(skills.SHIPPED)
    for name, ep in eps.items():
        d = ep.load()()
        assert os.path.basename(d) == name
        assert frontmatter(os.path.join(d, "SKILL.md"))["name"] == name


@pytest.fixture(scope="module")
def cli():
    """(the flags svcheck --help lists, the marker IDs svcheck knows)."""
    h = subprocess.run([native_path(), "--help"], capture_output=True, text=True)
    flags = set(re.findall(r"(--[a-z][a-z0-9-]*)", h.stdout))
    m = subprocess.run([native_path(), "--list-markers"], capture_output=True, text=True)
    return flags, set(re.findall(r"\b(SVCHK(?:-[A-Z]+)?\d{3})\b", m.stdout))


@pytest.mark.parametrize("path", md_files(), ids=lambda p: os.path.relpath(p, SKILLS))
def test_flags_and_ids_exist(cli, path):
    flags, ids = cli
    text = read(path)
    # svcheck's flags, where the text runs svcheck: `svcheck ... --flag`.
    used = set()
    for line in text.splitlines():
        for m in re.finditer(r"svcheck\b[^`|]*", line):
            used |= set(re.findall(r"(?<![\w-])(--[a-z][a-z0-9-]*)", m.group(0)))
    assert used - flags == set()
    named = set(re.findall(r"\b(SVCHK(?:-[A-Z]+)?\d{3})\b", text))
    assert named - ids == set()


def rule_blocks():
    out = []
    for path in md_files():
        text = read(path)
        for m in FENCE.finditer(text):
            info = m.group(1).split()
            if info[:1] == ["yaml"] and "no-check" not in info and "sv-rules:" in m.group(2):
                line = text.count("\n", 0, m.start()) + 1
                out.append(("%s:%d" % (os.path.relpath(path, SKILLS), line), m.group(2)))
    return out


@pytest.mark.skipif(not os.path.isfile(os.path.join(UVM, "uvm_pkg.sv")),
                    reason="UVM is not in packages/ (ivpm update)")
@pytest.mark.parametrize("where,body", rule_blocks(), ids=[w for w, _ in rule_blocks()])
def test_rule_files_check(tmp_path, where, body):
    f = tmp_path / "rules.yaml"
    f.write_text(body)
    r = subprocess.run([native_path(), "--check-rules", "--rules", str(f), "--uvm", UVM],
                       capture_output=True, text=True, timeout=300)
    assert r.returncode == 0, "%s:\n%s%s" % (where, r.stdout, r.stderr)
    assert " 0 failed" in r.stdout + r.stderr


def test_generated_references_are_current():
    from gen_skill_refs import OUT_DIR, SYNTAX_TXT, render_all
    from hooks import EngineHooks
    if not SYNTAX_TXT.is_file():
        pytest.skip("slang's sources are not in packages/ (ivpm update)")
    h = EngineHooks(semantic="model")
    try:
        files = render_all(h)
    finally:
        h.close()
    stale = [n for n, text in files.items() if (OUT_DIR / n).read_text(encoding="utf-8") != text]
    assert stale == [], "stale: %s; run scripts/gen_skill_refs.py" % ", ".join(stale)
