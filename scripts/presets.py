#!/usr/bin/env python3
"""Runs the built-in presets on the corpora (svcheck-plan P2.6) and prints,
as Markdown, how many findings each rule has under each preset against the
pack at its defaults (`--select SVCHK-UVM`), by house, then every finding of
the consistency rule (SVCHK-UVM039), which only a preset's conventions feed.

    scripts/presets.py [--svcheck BIN] [--jobs N] [PRESET ...]

The corpora are those scripts/precision.py reads (tests/corpora/corpora.yaml:
present and counting towards precision). A finding is its rule, its file as
its source (registry.source_path) and line, and its message, so a DV library
file that several OpenTitan IPs share is one finding, as in the precision
reviews. Houses: OpenTitan's IPs (lowRISC), CORE-V-VERIF, and the rest.
"""
import argparse
import collections
import concurrent.futures
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "corpora"))
import registry  # noqa: E402

ROOT = registry.ROOT
PRESETS = ["uvm-consensus", "easier-uvm", "lowrisc-dv", "core-v"]
DEFAULT = "(pack)"
CONSISTENCY = "SVCHK-UVM039"


def house(corpus):
    if corpus.startswith("opentitan-"):
        return "OpenTitan"
    if corpus.startswith("core-v-"):
        return "CORE-V"
    return "other"


def run(svcheck, corpus, preset):
    cmd = [svcheck] + (["--select", "SVCHK-UVM"] if preset == DEFAULT else ["--preset", preset])
    cmd += ["-F", os.path.relpath(corpus["path"], ROOT), "--format", "json"]
    p = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    if p.returncode not in (0, 1) or not p.stdout.strip():
        sys.exit("svcheck failed (%d) on %s with %s:\n%s" % (p.returncode, corpus["name"],
                                                            preset, p.stderr))
    return json.loads(p.stdout)["findings"]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--svcheck", default=os.path.join(ROOT, "python", "svcheck", "bin", "svcheck"))
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("presets", nargs="*")
    args = ap.parse_args()
    presets = [DEFAULT] + (args.presets or PRESETS)
    corpora = [c for c in registry.load() if c["present"] and c["precision"]]

    # (preset, house) -> {(rule, at, message)}
    found = collections.defaultdict(set)
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
        jobs = {ex.submit(run, args.svcheck, c, p): (c["name"], p)
                for c in corpora for p in presets}
        for fut, (name, p) in jobs.items():
            for f in fut.result():
                if not f["code"].startswith("SVCHK-UVM"):
                    continue
                at = "%s:%d" % (registry.source_path(f["file"]), f["line"])
                found[(p, house(name))].add((f["code"], at, f["message"]))

    houses = ["OpenTitan", "CORE-V", "other"]
    rules = sorted({k[0] for s in found.values() for k in s})
    print("### Findings per rule, by preset and house (%d corpora)\n" % len(corpora))
    cols = [(p, h) for h in houses for p in presets]
    print("| Rule | " + " | ".join("%s %s" % (h, p) for p, h in cols) + " |")
    print("|---|" + "---|" * len(cols))
    for r in rules:
        print("| %s | " % r + " | ".join(
            str(sum(1 for k in found[c] if k[0] == r)) for c in cols) + " |")
    print("| total | " + " | ".join(str(len(found[c])) for c in cols) + " |")

    print("\n### %s findings\n" % CONSISTENCY)
    for p in presets:
        for h in houses:
            for k in sorted(x for x in found[(p, h)] if x[0] == CONSISTENCY):
                print("- %s, %s: `%s` %s" % (p, h, k[1], k[2]))


if __name__ == "__main__":
    main()
