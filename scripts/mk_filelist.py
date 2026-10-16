#!/usr/bin/env python3
"""Writes a slang file list (`-f`) for a tree of SystemVerilog sources that
has no file list of its own (svcheck-plan P1.6, P1.7): an OpenTitan IP
outside FuseSoC, say.

    scripts/mk_filelist.py [--uvm DIR] [--single-unit] [--first F ...]
        [--incdir-tree D ...] LINT_DIR ...
    scripts/mk_filelist.py --rtlmeter DESIGN_DIR [--config NAME]

Every directory under each LINT_DIR and --incdir-tree is an include
directory. The files are every `.sv` under the LINT_DIRs that no file in
those trees `include`s (OpenTitan names its included class files `.sv`), in
path order. `--first` files come before them: with `--single-unit`, the
macros they define reach every later file, as in a flow that compiles one
unit. `--uvm` adds UVM's src directory and its uvm_pkg.sv.

`--rtlmeter` instead reads an rtlmeter design's `descriptor.yaml`: its
source files, the directories of its include files and its defines, for the
configuration named (`default`), compiled as one unit.
"""
import argparse
import os
import re
import sys

INCLUDE = re.compile(r'`include\s+"([^"]+)"')


def sources(dirs):
    for d in dirs:
        for root, subdirs, files in os.walk(d):
            subdirs.sort()
            for f in sorted(files):
                if f.endswith((".sv", ".svh")):
                    yield os.path.join(root, f)


def rtlmeter(design, config):
    import yaml
    with open(os.path.join(design, "descriptor.yaml"), encoding="utf-8") as fp:
        doc = yaml.safe_load(fp)
    base = doc.get("compile") or {}
    conf = ((doc.get("configurations") or {}).get(config) or {}).get("compile") or {}
    files = (base.get("verilogSourceFiles") or []) + (conf.get("verilogSourceFiles") or [])
    incs = (base.get("verilogIncludeFiles") or []) + (conf.get("verilogIncludeFiles") or [])
    defines = dict(base.get("verilogDefines") or {}, **(conf.get("verilogDefines") or {}))
    out = ["--single-unit"]
    out += ["+incdir+" + d for d in sorted({os.path.dirname(os.path.join(design, i))
                                            for i in incs})]
    out += ["+define+%s=%s" % (k, v) for k, v in defines.items()]
    out += [os.path.join(design, f) for f in files]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--rtlmeter", help="an rtlmeter design's directory")
    ap.add_argument("--config", default="default", help="the rtlmeter configuration")
    ap.add_argument("--uvm", help="UVM's src directory")
    ap.add_argument("--single-unit", action="store_true")
    ap.add_argument("--first", nargs="*", default=[], help="files to put first")
    ap.add_argument("--incdir-tree", nargs="*", default=[],
                    help="trees whose directories are include directories, not linted")
    ap.add_argument("lint", nargs="*", help="trees to lint")
    a = ap.parse_args()
    if a.rtlmeter:
        sys.stdout.write("\n".join(rtlmeter(os.path.abspath(a.rtlmeter), a.config)) + "\n")
        return
    if not a.lint:
        ap.error("no tree to lint")
    lint = [os.path.abspath(d) for d in a.lint]
    trees = lint + [os.path.abspath(d) for d in a.incdir_tree]

    included = set()
    for f in sources(trees):
        with open(f, encoding="utf-8", errors="replace") as fp:
            included.update(os.path.basename(m) for m in INCLUDE.findall(fp.read()))

    out = []
    if a.single_unit:
        out.append("--single-unit")
    if a.uvm:
        uvm = os.path.abspath(a.uvm)
        out += ["+incdir+" + uvm, os.path.join(uvm, "uvm_pkg.sv"),
                os.path.join(uvm, "uvm_macros.svh")]
    for t in trees:
        for root, subdirs, _ in os.walk(t):
            subdirs.sort()
            out.append("+incdir+" + root)
    first = [os.path.abspath(f) for f in a.first]
    out += first
    out += [f for f in sources(lint)
            if f.endswith(".sv") and os.path.basename(f) not in included and f not in first]
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
