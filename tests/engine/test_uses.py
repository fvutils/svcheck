"""`uses:` and file-level options (svcheck-plan P1.4; svcheck-design §4).

A rule file names helper files in `uses:`, relative to itself; their
`define:` helpers and `options:` join the file's, transitively. A helper
file holds helpers only. File-level `options:` are read as `options.<name>`
by every rule of the file and by the helpers, at their defaults until config
sets them (P2).
"""
import pytest

SRC = "class ab; endclass\nclass c; endclass\nclass Up; endclass\n"


def _rules(where, extra="", rid="ACME001"):
    return ("sv-rules:\n" + extra +
            "  rules:\n"
            "    - id: %s\n"
            "      name: r-%s\n"
            "      tier: style\n"
            "      summary: s\n"
            "      message: \"class '${node.name.text}'\"\n"
            "      match: { kind: ClassDeclaration }\n"
            "      where: \"%s\"\n" % (rid, rid.lower(), where))


def _run(hooks, path, text):
    path.write_text(text)
    return hooks.lint_run(str(path), text, SRC)


def _errors(r):
    return [(d["code"], d["file"].rsplit("/", 1)[-1], d["line"], d["message"])
            for d in r["diagnostics"] if d["severity"] == "error"]


def _lines(r):
    return [f["line"] for f in r["findings"]]


def test_a_used_helper(engine_hooks, tmp_path):
    (tmp_path / "vocab.yaml").write_text(
        "sv-rules:\n"
        "  define:\n"
        "    short(n): n.name.text.size() < 3\n")
    r = _run(engine_hooks, tmp_path / "r.yaml",
             _rules("short(node)", "  uses: [vocab.yaml]\n"))
    assert r["loaded"] and _errors(r) == [], r["diagnostics"]
    assert _lines(r) == [1, 2, 3]


def test_uses_are_transitive_and_relative_to_the_file(engine_hooks, tmp_path):
    (tmp_path / "lib").mkdir()
    (tmp_path / "lib" / "a.yaml").write_text(
        "sv-rules:\n  uses: [b.yaml]\n  define:\n    lower(n): low(n.name.text)\n")
    (tmp_path / "lib" / "b.yaml").write_text(
        "sv-rules:\n  define:\n    low(s): s.matches('^[a-z]')\n")
    (tmp_path / "rules").mkdir()
    r = _run(engine_hooks, tmp_path / "rules" / "r.yaml",
             _rules("!lower(node)", "  uses: [../lib/a.yaml]\n"))
    assert _errors(r) == [], r["diagnostics"]
    assert _lines(r) == [3]


def test_a_file_used_twice_loads_once(engine_hooks, tmp_path):
    (tmp_path / "a.yaml").write_text("sv-rules:\n  uses: [c.yaml]\n")
    (tmp_path / "b.yaml").write_text("sv-rules:\n  uses: [c.yaml]\n")
    (tmp_path / "c.yaml").write_text("sv-rules:\n  define:\n    'yes': 'true'\n")
    r = _run(engine_hooks, tmp_path / "r.yaml", _rules("yes", "  uses: [a.yaml, b.yaml]\n"))
    assert _errors(r) == [] and _lines(r) == [1, 2, 3], r["diagnostics"]


def test_file_options(engine_hooks, tmp_path):
    (tmp_path / "vocab.yaml").write_text(
        "sv-rules:\n"
        "  options:\n"
        "    min: { type: int, default: 3, help: shortest name }\n"
        "  define:\n"
        "    short(n): n.name.text.size() < options.min\n")
    extra = ("  uses: [vocab.yaml]\n"
             "  options:\n"
             "    exempt: { type: string, default: c, help: name not checked }\n")
    r = _run(engine_hooks, tmp_path / "r.yaml",
             _rules("short(node) && node.name.text != options.exempt", extra))
    assert _errors(r) == [], r["diagnostics"]
    assert _lines(r) == [1, 3]


def test_a_rule_option_may_not_reuse_a_file_option(engine_hooks, tmp_path):
    text = _rules("true", "  options:\n    min: { type: int, default: 3, help: h }\n")
    text += "      options:\n        min: { type: int, default: 4, help: h }\n"
    r = _run(engine_hooks, tmp_path / "r.yaml", text)
    ((code, file, line, msg),) = _errors(r)
    assert (code, file, line) == ("SVCHK212", "r.yaml", 13)
    assert "also a file option" in msg


def test_a_cycle_is_refused(engine_hooks, tmp_path):
    (tmp_path / "a.yaml").write_text("sv-rules:\n  uses: [b.yaml]\n")
    (tmp_path / "b.yaml").write_text("sv-rules:\n  uses: [a.yaml]\n")
    r = _run(engine_hooks, tmp_path / "r.yaml", _rules("true", "  uses: [a.yaml]\n"))
    ((code, file, line, msg),) = _errors(r)
    assert (code, file, line) == ("SVCHK210", "b.yaml", 2)
    assert msg.startswith("'uses' makes a cycle: ") and msg.endswith("a.yaml")


def test_an_unreadable_file_is_named(engine_hooks, tmp_path):
    r = _run(engine_hooks, tmp_path / "r.yaml", _rules("true", "  uses: [missing.yaml]\n"))
    ((code, file, line, msg),) = _errors(r)
    assert (code, file, line) == ("SVCHK210", "r.yaml", 2)
    assert msg == "cannot read '%s', which 'uses' names" % (tmp_path / "missing.yaml")


def test_a_helper_file_has_no_rules(engine_hooks, tmp_path):
    (tmp_path / "vocab.yaml").write_text("sv-rules:\n  rules: []\n")
    r = _run(engine_hooks, tmp_path / "r.yaml", _rules("true", "  uses: [vocab.yaml]\n"))
    ((code, file, line, msg),) = _errors(r)
    assert (code, file, line) == ("SVCHK210", "vocab.yaml", 2)
    assert "holds helpers only" in msg


def test_helpers_clash_and_shadow(engine_hooks, tmp_path):
    (tmp_path / "a.yaml").write_text("sv-rules:\n  define:\n    h: 'true'\n")
    (tmp_path / "b.yaml").write_text("sv-rules:\n  define:\n    h: 'false'\n    g: 'true'\n")
    r = _run(engine_hooks, tmp_path / "r.yaml",
             _rules("h", "  uses: [a.yaml, b.yaml]\n  define:\n    g: 'false'\n"))
    ((code, file, line, msg),) = _errors(r)
    assert (code, file, line) == ("SVCHK212", "b.yaml", 3)
    assert "also defined in" in msg
    infos = [(d["code"], d["line"], d["message"]) for d in r["diagnostics"]
             if d["severity"] == "info"]
    assert [(c, l) for c, l, _ in infos] == [("SVCHK219", 4)]
    assert infos[0][2].endswith("b.yaml")


def test_an_error_in_a_used_helper_is_reported_in_its_file(engine_hooks, tmp_path):
    (tmp_path / "vocab.yaml").write_text(
        "sv-rules:\n  define:\n    bad(n): n.name.text + 1\n")
    r = _run(engine_hooks, tmp_path / "r.yaml", _rules("bad(node)", "  uses: [vocab.yaml]\n"))
    files = {(d["code"], d["file"].rsplit("/", 1)[-1], d["line"]) for d in r["diagnostics"]
             if d["severity"] == "error"}
    assert ("SVCHK215", "vocab.yaml", 3) in files, r["diagnostics"]


@pytest.mark.parametrize("text,msg", [
    ("sv-rules:\n  uses: vocab.yaml\n", "'uses' must be a list of file paths"),
    ("sv-rules:\n  uses: [3]\n", "a 'uses' entry must be a file path"),
])
def test_uses_must_be_a_list_of_paths(engine_hooks, tmp_path, text, msg):
    r = _run(engine_hooks, tmp_path / "r.yaml",
             _rules("true", text.split("\n", 1)[1]))
    assert any(msg in d["message"] for d in r["diagnostics"]), r["diagnostics"]
