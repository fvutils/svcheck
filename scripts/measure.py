#!/usr/bin/env python3
"""Measures svcheck on the corpora (svcheck-plan P1.7) and prints the
tables for the plan's §6, as Markdown.

    scripts/measure.py [--svcheck BIN] [--runs N] [CORPUS ...]

CORPUS is a corpus of tests/corpora/corpora.yaml (default: every one that is
present and compiles, P2.1; P1.7's tables were of uvm-tb-proto, fw-wb-dma,
opentitan-uart and fwvip).
Each configuration runs N times (default 5) with `--stats`; each phase's
time is the minimum over the runs, the heap is the last run's. The tables:

1. **Phases** on the curated model, with the spike's fw-hdl and Verible rules and the
   UVM pack (`--select SVCHK-UVM`, with its content classifiers): parse, elaborate (slang's
   compile and bind, every diagnostic), lower, classify, rules; and the
   heap after each. *Lower %* is lowering against parse + elaborate, Q-SC5's
   criterion (design §3.6: adopt the model under about 20%).
2. **Classify**: the classify stage with the pack's classifiers (content), and with
   the convention set (tests/data/classify-convention.yaml) beside them.

The Q-SC5 table (the 4 UVM rules on each semantic layer) went with the
slang layer at P1.8; its last output is in svcheck-plan §6.1.
"""
import argparse
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "corpora"))
import registry  # noqa: E402

ROOT = registry.ROOT
SPIKE = ["rules/spike/fw-hdl.yaml", "rules/spike/verible.yaml"]
# The built-in UVM pack (svcheck-plan P2.5), and its classifiers with it.
PACK = ["--select", "SVCHK-UVM"]
CONVENTION = "tests/data/classify-convention.yaml"
PHASES = ["parse_s", "elaborate_s", "lower_s", "classify_s", "rules_s", "index_s", "total_s"]
MB = 1024.0 * 1024.0


def run(svcheck, corpus, rules, layer="model"):
    cmd = [svcheck] + PACK
    for r in rules:
        cmd += ["--rules", r]
    cmd += ["-F", os.path.relpath(registry.load([corpus])[0]["path"], ROOT), "--format", "json",
            "--stats",
            "--semantic", layer]
    p = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    if p.returncode not in (0, 1) or not p.stdout.strip():
        sys.exit("svcheck failed (%d) on %s:\n%s" % (p.returncode, corpus, p.stderr))
    return json.loads(p.stdout)


def best(svcheck, runs, corpus, rules, layer="model"):
    """The last run's output, with each phase's minimum over the runs."""
    outs = [run(svcheck, corpus, rules, layer) for _ in range(runs)]
    out = outs[-1]
    for k in PHASES:
        out["stats"][k] = min(o["stats"][k] for o in outs)
    for i, r in enumerate(out["stats"]["rules"]):
        r["s"] = min(o["stats"]["rules"][i]["s"] for o in outs)
    return out


def ms(s):
    return "%.0f" % (s * 1000.0) if s >= 0.0095 else "%.1f" % (s * 1000.0)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--svcheck", default=os.path.join(ROOT, "python", "svcheck", "bin", "svcheck"))
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("corpora", nargs="*")
    args = ap.parse_args()
    if not args.corpora:
        args.corpora = [c["name"] for c in registry.load() if c["present"] and not c["exception"]]

    phases, classify = [], []
    for c in args.corpora:
        out = best(args.svcheck, args.runs, c, SPIKE)
        phases.append((c, out))
        conv = best(args.svcheck, args.runs, c, [CONVENTION])
        classify.append((c, out, conv))

    print("### Phases: curated model, the spike rules (fw-hdl, verible) and the UVM pack "
          "(ms, min of %d)\n" % args.runs)
    print("| Corpus | Files | Parse | Elaborate | Lower | Lower % | Classify | Index | Rules "
          "| Total | Heap: elaborated → lowered (MB) | Model heap % | Peak RSS (MB) |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    for c, out in phases:
        st, h = out["stats"], out["stats"]["heap"]
        compile_s = st["parse_s"] + st["elaborate_s"]
        lowered = h["lowered"] - h["elaborated"]
        print("| %s | %d | %s | %s | %s | %.0f%% | %s | %s | %s | %s | %.0f → %.0f | %.0f%% | %.0f |" % (
            c, len(out["compilation"]["files"]), ms(st["parse_s"]), ms(st["elaborate_s"]),
            ms(st["lower_s"]), 100.0 * st["lower_s"] / compile_s, ms(st["classify_s"]),
            ms(st["index_s"]), ms(st["rules_s"]), ms(st["total_s"]), h["elaborated"] / MB,
            h["lowered"] / MB, 100.0 * lowered / h["elaborated"], st["max_rss"] / MB))

    print("\n### Rules: time per rule on the model (ms, min of %d)\n" % args.runs)
    ids = [r["id"] for r in phases[0][1]["stats"]["rules"]]
    print("| Corpus | " + " | ".join(ids) + " |")
    print("|---|" + "---|" * len(ids))
    for c, out in phases:
        t = {r["id"]: r["s"] for r in out["stats"]["rules"]}
        print("| %s | " % c + " | ".join(ms(t[i]) for i in ids) + " |")

    print("\n### Classify: the pack's classifiers, and convention beside them (ms, min of %d)\n" % args.runs)
    print("| Corpus | Content | Content + convention |")
    print("|---|---|---|")
    for c, content, conv in classify:
        print("| %s | %s | %s |" % (c, ms(content["stats"]["classify_s"]),
                                   ms(conv["stats"]["classify_s"])))


if __name__ == "__main__":
    main()
