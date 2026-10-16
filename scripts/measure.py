#!/usr/bin/env python3
"""Measures svcheck on the corpora (svcheck-plan P1.7) and prints the
tables for the plan's §6, as Markdown.

    scripts/measure.py [--svcheck BIN] [--runs N] [CORPUS ...]

CORPUS is a `tests/corpora/<name>.f` (default: the trial's three and fwvip).
Each configuration runs N times (default 5) with `--stats`; each phase's
time is the minimum over the runs, the heap is the last run's. The tables:

1. **Phases** on the curated model, with the spike's 10 rules and the
   contexts trial's (content classifiers): parse, elaborate (slang's
   compile and bind, every diagnostic), lower, classify, rules; and the
   heap after each. *Lower %* is lowering against parse + elaborate, Q-SC5's
   criterion (design §3.6: adopt the model under about 20%).
2. **Q-SC5 layers**: the same 4 UVM rules on each layer
   (tests/host/rules/uvm-{model,slang}.yaml): the layer's build, the rules'
   time and their findings.
3. **Classify**: the classify stage with each classifier set of the trial.
"""
import argparse
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORPORA = ["uvm-tb-proto", "fw-wb-dma", "opentitan-uart", "fwvip"]
SPIKE = ["rules/spike/uvm.yaml", "rules/spike/fw-hdl.yaml", "rules/spike/verible.yaml"]
TRIAL = "rules/trial/contexts.yaml"
CONVENTION = "rules/trial/classify-convention.yaml"
PHASES = ["parse_s", "elaborate_s", "lower_s", "classify_s", "rules_s", "index_s", "total_s"]
MB = 1024.0 * 1024.0


def run(svcheck, corpus, rules, layer="model"):
    cmd = [svcheck]
    for r in rules:
        cmd += ["--rules", r]
    cmd += ["-F", "tests/corpora/%s.f" % corpus, "--format", "json", "--stats",
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
    ap.add_argument("corpora", nargs="*", default=CORPORA)
    args = ap.parse_args()

    phases, layers, classify = [], [], []
    for c in args.corpora:
        out = best(args.svcheck, args.runs, c, SPIKE + [TRIAL])
        phases.append((c, out))
        q5 = {}
        for layer in ("model", "slang"):
            q5[layer] = best(args.svcheck, args.runs, c,
                             ["tests/host/rules/uvm-%s.yaml" % layer], layer)
        layers.append((c, q5))
        conv = best(args.svcheck, args.runs, c, [TRIAL, CONVENTION])
        classify.append((c, out, conv))

    print("### Phases: curated model, the 10 spike rules and the 3 trial rules "
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

    print("\n### Q-SC5: the 4 UVM rules on each layer (ms, min of %d)\n" % args.runs)
    print("| Corpus | Model: lower | Model: rules | Model: findings | Slang: build "
          "| Slang: rules | Slang: findings | Only the model finds |")
    print("|---|---|---|---|---|---|---|---|")
    for c, q5 in layers:
        def found(o):
            return {(f["code"], f["file"], f["line"]) for f in o["findings"]
                    if not f["code"].startswith("SVCHK")}
        m, s = q5["model"], q5["slang"]
        print("| %s | %s | %s | %d | %s | %s | %d | %d |" % (
            c, ms(m["stats"]["lower_s"]), ms(m["stats"]["rules_s"]), len(found(m)),
            ms(s["stats"]["lower_s"]), ms(s["stats"]["rules_s"]), len(found(s)),
            len(found(m) - found(s))))

    print("\n### Classify: the trial's classifier sets (ms, min of %d)\n" % args.runs)
    print("| Corpus | Content | Content + convention |")
    print("|---|---|---|")
    for c, content, conv in classify:
        print("| %s | %s | %s |" % (c, ms(content["stats"]["classify_s"]),
                                   ms(conv["stats"]["classify_s"])))


if __name__ == "__main__":
    main()
