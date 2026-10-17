#!/usr/bin/env python3
"""The precision harness (svcheck-plan P2.1; lint-criteria §5.4): runs rules on the
corpora, matches each finding with its review in tests/precision/<RULE>.yaml, and
prints each rule's precision against its tier's bar, as Markdown.

    scripts/precision.py [--svcheck BIN] [--rules FILE ...] [--corpus NAME ...]
                         [--update] [--prune] [--check] [--list RULE]

The corpora are those of tests/corpora/corpora.yaml that are present and count
towards precision (`precision: true`); --corpus names others. The rules default to
RULES. Each corpus is compiled once with every rule file.

**Reviews.** A finding is the rule, the file and line, and the message. The file is
named as its source (registry.source_path), so a file that several OpenTitan IPs
share is one finding, listed with each corpus it was found in. Its verdict:

- `true`: a real problem, worth acting on;
- `false`: wrong; the rule misread the code;
- `out-of-scope`: right by the rule's letter, but nobody would act on it (code
  the rule should not have looked at, an intended use). Like `false`, this is
  an *effective* false positive (lint-criteria §2.2);
- `unreviewed`.

Precision is true / (reviewed). The bar is the tier's: `correctness` has no
effective false positive, `suspicious` and `portability` fewer than 10%; other
tiers have no bar, and the rate is only reported. A rule is *incomplete* while it
has unreviewed findings, unless those reviewed already miss its bar.

`--update` adds new findings to the review files as unreviewed and keeps every
verdict. `--prune` also drops reviews of findings no longer reported (`gone`;
only when every corpus that reported them was run). `--check` exits 1 if a
finding is new (not in its review file) or gone, which is how a regression
shows; run --update and review what it added. `--list RULE` prints that rule's
findings with their verdicts.
"""
import argparse
import concurrent.futures
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "corpora"))
import registry  # noqa: E402
import yaml  # noqa: E402

ROOT = registry.ROOT
REVIEWS = os.path.join(ROOT, "tests", "precision")
RULES = ["rules/spike/fw-hdl.yaml", "rules/spike/verible.yaml",
         "rules/trial/contexts.yaml"]
# The built-in packs, which follow the profile's tiers, are selected by
# namespace (svcheck-plan P2.5).
SELECT = ["SVCHK-UVM"]
VERDICTS = ("true", "false", "out-of-scope", "unreviewed")
BARS = {"correctness": 0.0, "suspicious": 0.10, "portability": 0.10}
HEADER = """\
# Precision review of {rule} (scripts/precision.py; svcheck-plan P2.1). Each finding
# is the file and line, as its source, and the message; `corpora` says where it was
# found. verdict: true (worth acting on), false (wrong), out-of-scope (right by the
# rule's letter, but nobody would act on it), or unreviewed. Say why in `note`.
"""


def run(svcheck, corpus, rules):
    cmd = [svcheck]
    for r in rules:
        cmd += ["--rules", r]
    for s in SELECT:
        cmd += ["--select", s]
    cmd += ["-F", os.path.relpath(corpus["path"], ROOT), "--format", "json"]
    p = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    if p.returncode not in (0, 1) or not p.stdout.strip():
        sys.exit("svcheck failed (%d) on %s:\n%s" % (p.returncode, corpus["name"], p.stderr))
    return json.loads(p.stdout)


def review_path(rule):
    return os.path.join(REVIEWS, rule + ".yaml")


def load_review(rule):
    path = review_path(rule)
    if not os.path.exists(path):
        return []
    with open(path) as f:
        doc = yaml.safe_load(f) or {}
    out = doc.get("findings") or []
    for e in out:
        if e.get("verdict") not in VERDICTS:
            sys.exit("%s: %s: verdict must be one of %s" % (path, e.get("at"), ", ".join(VERDICTS)))
    return out


def save_review(rule, entries):
    os.makedirs(REVIEWS, exist_ok=True)
    entries = sorted(entries, key=lambda e: (e["at"].rsplit(":", 1)[0],
                                             int(e["at"].rsplit(":", 1)[1]), e["message"]))
    body = yaml.safe_dump({"rule": rule, "findings": entries}, sort_keys=False, width=100,
                          allow_unicode=True)
    with open(review_path(rule), "w") as f:
        f.write(HEADER.format(rule=rule) + body)


def collect(svcheck, corpora, rules, jobs):
    """{rule: {(at, message): set of corpora}}, the tiers, and each corpus's run."""
    found, tiers, runs = {}, {}, {}
    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        outs = dict(zip([c["name"] for c in corpora],
                        ex.map(lambda c: run(svcheck, c, rules), corpora)))
    for c in corpora:
        out = outs[c["name"]]
        runs[c["name"]] = out
        for r in out["rules"]:
            tiers[r["id"]] = r.get("tier", "")
            found.setdefault(r["id"], {})
        for f in out["findings"]:
            if f["code"] not in tiers:
                continue  # an engine or host marker, not a rule's finding
            key = ("%s:%d" % (registry.source_path(f["file"]), f["line"]), f["message"])
            found[f["code"]].setdefault(key, set()).add(c["name"])
    return found, tiers, runs


def merge(rule, found, ran):
    """The review entries, updated with this run, and the gone ones."""
    entries = load_review(rule)
    keyed = {(e["at"], e["message"]): e for e in entries}
    for key, where in found.items():
        e = keyed.get(key)
        if e is None:
            e = {"at": key[0], "message": key[1], "corpora": [], "verdict": "unreviewed",
                 "note": ""}
            keyed[key] = e
            entries.append(e)
        e["corpora"] = sorted(set(e.get("corpora") or []) | where)
    gone = [e for e in entries if (e["at"], e["message"]) not in found
            and set(e.get("corpora") or []) <= ran]
    return entries, gone


def status(tier, counts):
    reviewed = counts["true"] + counts["false"] + counts["out-of-scope"]
    total = reviewed + counts["unreviewed"]
    if total == 0:
        return "—", "no findings"
    if reviewed == 0:
        return "—", "unreviewed"
    fp = (counts["false"] + counts["out-of-scope"]) / reviewed
    prec = "%.0f%%" % (100.0 * (1.0 - fp))
    bar = BARS.get(tier)
    if bar is None:
        verdict = "no bar"
    elif (fp > 0 if bar == 0.0 else fp >= bar):
        verdict = "below bar"
    else:
        verdict = "meets bar"
    if counts["unreviewed"] and verdict != "below bar":
        verdict = "incomplete"
    return prec, verdict


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--svcheck", default=os.path.join(ROOT, "python", "svcheck", "bin", "svcheck"))
    ap.add_argument("--rules", action="append", help="a rule file (default: RULES)")
    ap.add_argument("--corpus", action="append", help="a corpus of corpora.yaml (default: all "
                    "present with precision: true)")
    ap.add_argument("--update", action="store_true", help="add new findings to the reviews")
    ap.add_argument("--prune", action="store_true", help="with --update, drop gone findings")
    ap.add_argument("--check", action="store_true", help="exit 1 on a new or gone finding")
    ap.add_argument("--list", metavar="RULE", help="print one rule's findings and verdicts")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()

    if args.corpus:
        corpora = registry.load(args.corpus)
        missing = [c["name"] for c in corpora if not c["present"]]
        if missing:
            sys.exit("not present: %s (tests/corpora/README.md)" % " ".join(missing))
    else:
        corpora = [c for c in registry.load() if c["present"] and c["precision"]]
    skipped = [c["name"] for c in registry.load()
               if c["precision"] and not c["present"] and not args.corpus]
    found, tiers, runs = collect(args.svcheck, corpora, args.rules or RULES, args.jobs)
    ran = {c["name"] for c in corpora}

    if args.list:
        entries, _ = merge(args.list, found.get(args.list, {}), ran)
        for e in sorted(entries, key=lambda e: e["at"]):
            live = (e["at"], e["message"]) in found.get(args.list, {})
            print("%-12s %s%s\n    %s\n    %s%s" % (e["verdict"], e["at"], "" if live else "  (gone)",
                                                  e["message"], ", ".join(e["corpora"]),
                                                  ("\n    note: " + e["note"]) if e.get("note") else ""))
        return

    print("### Corpora\n")
    print("| Corpus | Files | Errors | Findings |")
    print("|---|---|---|---|")
    for c in corpora:
        out = runs[c["name"]]
        n = sum(1 for f in out["findings"] if f["code"] in tiers)
        print("| %s | %d | %d%s | %d |" % (c["name"], len(out["compilation"]["files"]),
                                          out["compilation"]["errors"],
                                          " (exception)" if c["exception"] else "", n))
    if skipped:
        print("\nNot present, skipped: %s." % ", ".join(skipped))

    print("\n### Precision\n")
    print("| Rule | Tier | Findings | True | False | Out of scope | Unreviewed | New | Gone "
          "| Precision | Status |")
    print("|---|---|---|---|---|---|---|---|---|---|---|")
    bad = False
    for rule in sorted(found):
        known = {(e["at"], e["message"]) for e in load_review(rule)}
        new = sum(1 for k in found[rule] if k not in known)
        entries, gone = merge(rule, found[rule], ran)
        live = [e for e in entries if (e["at"], e["message"]) in found[rule]]
        counts = {v: sum(1 for e in live if e["verdict"] == v) for v in VERDICTS}
        prec, verdict = status(tiers.get(rule, ""), counts)
        print("| %s | %s | %d | %d | %d | %d | %d | %d | %d | %s | %s |" % (
            rule, tiers.get(rule) or "—", len(live), counts["true"], counts["false"],
            counts["out-of-scope"], counts["unreviewed"], new, len(gone), prec, verdict))
        bad |= bool(new or gone)
        if args.update:
            keep = [e for e in entries if not (args.prune and e in gone)]
            if keep or os.path.exists(review_path(rule)):
                save_review(rule, keep)
    if args.check and bad:
        sys.exit(1)


if __name__ == "__main__":
    main()
