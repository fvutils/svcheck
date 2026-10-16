#!/usr/bin/env python3
"""Runs Verible and svcheck on the same files and diffs their findings
(svcheck-plan P1.6; svcheck-design §9, "Verible equivalence").

svcheck lints a compilation, given as slang's own file arguments (`-f`,
`+incdir+`, `+define+`, files). Verible lints files one at a time, so it is
given every user file that compilation read, `include`d ones too: the
`compilation.files` of svcheck's JSON output. Library code (uvm_pkg,
`--library-dir`) is left out of both.

A rule file's rules are paired with Verible's by `name`: each rule's name is
the Verible rule it is equivalent to (rules/spike/verible.yaml). Findings are
compared by file, line and rule. Columns are reported, not compared: the two
tools may point at different tokens of one construct.

    scripts/verible_equiv.py [--rules R] [--verible BIN] [--svcheck BIN]
        [--json OUT] -- <svcheck file arguments>

Prints the findings that only one tool has, each with the reason found for
it when there is one, and a summary. Exit status: 0 when the diff is empty,
1 when it is not, 2 when a tool could not be run.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys

import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_RULES = os.path.join(ROOT, "rules", "spike", "verible.yaml")
DEFAULT_VERIBLE = os.path.join(ROOT, "packages", "verible", "bin", "verible-verilog-lint")
DEFAULT_SVCHECK = os.path.join(ROOT, "python", "svcheck", "bin", "svcheck")

# `path:line:col: message [Style: topic] [rule-name]`; col may be a range.
VERIBLE_LINE = re.compile(r"^(?P<file>.+?):(?P<line>\d+):(?P<col>\d+)(?:-\d+)?:\s*"
                          r"(?P<msg>.*?)\s*\[(?P<rule>[a-z0-9-]+)\]\s*$")
VERIBLE_SYNTAX = re.compile(r"^(?P<file>.+?):(?P<line>\d+):(?P<col>\d+)(?:-\d+)?:\s*"
                            r"(syntax|preprocessing) error")


def rule_names(path):
    """svcheck rule ID -> name (= Verible's rule) for the rule file's rules."""
    with open(path, encoding="utf-8") as fp:
        doc = yaml.safe_load(fp)
    return {r["id"]: r["name"] for r in doc["sv-rules"]["rules"]}


def run_svcheck(binary, rules, args):
    cmd = [binary, "--rules", rules, "--semantic", "none", "--format", "json"] + args
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode not in (0, 1) or not p.stdout.strip():
        sys.stderr.write("svcheck failed (%d): %s\n%s" % (p.returncode, " ".join(cmd), p.stderr))
        sys.exit(2)
    return json.loads(p.stdout)


def run_verible(binary, names, files):
    cmd = [binary, "--ruleset=none", "--rules=" + ",".join(sorted(set(names))),
           "--lint_fatal=false", "--parse_fatal=false"] + files
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode not in (0, 1):
        sys.stderr.write("verible failed (%d): %s\n%s" % (p.returncode, " ".join(cmd), p.stderr))
        sys.exit(2)
    found, unparsed = [], set()
    for line in (p.stdout + "\n" + p.stderr).splitlines():
        m = VERIBLE_LINE.match(line)
        if m:
            found.append({"file": os.path.abspath(m["file"]), "line": int(m["line"]),
                          "col": int(m["col"]), "rule": m["rule"], "message": m["msg"]})
            continue
        m = VERIBLE_SYNTAX.match(line)
        if m:
            unparsed.add(os.path.abspath(m["file"]))
    return found, unparsed


def explain(f, other_unparsed, lines):
    """Why a finding only one tool has may be expected; None when unexplained."""
    if f["file"] in other_unparsed:
        return "the other tool could not parse the file"
    kinds = lines.get((f["file"], f["line"]), set())
    if "conditional" in kinds:
        return ("in `ifdef'd code: svcheck lints the branches the compilation's defines "
                "select, Verible lints the text")
    if "macro" in kinds:
        return "on a line with a macro use: svcheck sees the expansion, Verible the call"
    return None


DIRECTIVES = {"include", "define", "undef", "ifdef", "ifndef", "elsif", "else", "endif",
              "timescale", "resetall", "default_nettype", "line", "pragma", "celldefine",
              "endcelldefine", "begin_keywords", "end_keywords", "unconnected_drive",
              "nounconnected_drive", "__FILE__", "__LINE__"}


def line_kinds(files):
    """(file, line) -> {"macro", "conditional"}: the lines that use a macro, and
    those inside an `ifdef/`ifndef block (a heuristic: by text, not by the
    defines in force)."""
    out = {}
    for path in files:
        depth = 0
        try:
            with open(path, encoding="utf-8", errors="replace") as fp:
                for i, text in enumerate(fp, 1):
                    kinds = set()
                    for m in re.finditer(r"`(\w+)", text):
                        d = m.group(1)
                        if d in ("ifdef", "ifndef"):
                            depth += 1
                        elif d == "endif":
                            depth = max(depth - 1, 0)
                        elif d not in DIRECTIVES:
                            kinds.add("macro")
                    if depth:
                        kinds.add("conditional")
                    if kinds:
                        out[(path, i)] = kinds
        except OSError:
            pass
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--rules", default=DEFAULT_RULES)
    ap.add_argument("--verible", default=DEFAULT_VERIBLE)
    ap.add_argument("--svcheck", default=DEFAULT_SVCHECK)
    ap.add_argument("--json", help="also write the diff as JSON to this file")
    ap.add_argument("args", nargs="+", help="svcheck's file arguments (after --)")
    a = ap.parse_args()
    for tool in (a.verible, a.svcheck):
        if not (os.path.isfile(tool) or shutil.which(tool)):
            sys.stderr.write("not found: %s\n" % tool)
            return 2

    names = rule_names(a.rules)
    sv = run_svcheck(a.svcheck, a.rules, a.args)
    files = [os.path.abspath(f) for f in sv["compilation"]["files"]]
    sv_unparsed = {os.path.abspath(f) for f in sv["compilation"]["unparsed"]}
    sv_found = [{"file": os.path.abspath(f["file"]), "line": f["line"], "col": f["col"],
                 "rule": names[f["code"]], "message": f["message"]}
                for f in sv["findings"] if f["code"] in names and f["file"]]
    vb_found, vb_unparsed = run_verible(a.verible, names.values(), files)

    key = lambda f: (f["file"], f["line"], f["rule"])  # noqa: E731
    sv_keys = {key(f) for f in sv_found}
    vb_keys = {key(f) for f in vb_found}
    lines = line_kinds(files)
    only_sv = [dict(f, why=explain(f, vb_unparsed, lines))
               for f in sv_found if key(f) not in vb_keys]
    only_vb = [dict(f, why=explain(f, sv_unparsed, lines))
               for f in vb_found if key(f) not in sv_keys]
    both = len(sv_keys & vb_keys)

    cwd = os.getcwd()
    rel = lambda p: os.path.relpath(p, cwd)  # noqa: E731
    for title, fs in (("svcheck only", only_sv), ("Verible only", only_vb)):
        for f in sorted(fs, key=key):
            print("%s: %s:%d:%d [%s] %s%s" % (title, rel(f["file"]), f["line"], f["col"],
                  f["rule"], f["message"], "" if f["why"] is None else "  -- " + f["why"]))
    unexplained = [f for f in only_sv + only_vb if f["why"] is None]
    print("files %d (svcheck unparsed %d, Verible unparsed %d); agree %d; svcheck only %d; "
          "Verible only %d; unexplained %d" % (len(files), len(sv_unparsed), len(vb_unparsed),
                                              both, len(only_sv), len(only_vb),
                                              len(unexplained)))
    if a.json:
        with open(a.json, "w", encoding="utf-8") as fp:
            json.dump({"files": [rel(f) for f in files], "agree": both,
                       "svcheck_only": only_sv, "verible_only": only_vb,
                       "svcheck_unparsed": sorted(map(rel, sv_unparsed)),
                       "verible_unparsed": sorted(map(rel, vb_unparsed))}, fp, indent=2)
    return 0 if not (only_sv or only_vb) else 1


if __name__ == "__main__":
    sys.exit(main())
