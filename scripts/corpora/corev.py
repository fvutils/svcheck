#!/usr/bin/env python3
"""Writes tests/corpora/core-v-cv32e40p.f, the CV32E40P UVM bench of CORE-V-VERIF
(svcheck-plan P2.1), from the bench's own file lists.

    scripts/corpora/corev.py

The sources are ivpm's `corpora` dep-set: packages/core-v-verif, packages/cv32e40p
and packages/svlib. The bench's makefiles (mk/uvmt/uvmt.mk, dsim.mk) compile
uvm_pkg, the core's manifest and `uvmt_cv32e40p.flist`, whose nested lists name
their directories through make variables (DV_UVMT_PATH, ...). This expands them
to paths relative to tests/corpora, inlines each `-f`, and drops a file named a
second time (the lists name the OBI agent twice, and the tb and dut wrapper under
two variables), which the simulators accept and slang would not. The defines are
those of the makefiles' default configuration, without a reference model.
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CORPORA = os.path.join(ROOT, "tests", "corpora")
OUT = os.path.join(CORPORA, "core-v-cv32e40p.f")
CVV = "../../packages/core-v-verif"

VARS = {
    "DESIGN_RTL_DIR": "../../packages/cv32e40p/rtl",
    "DV_UVMT_PATH": CVV + "/cv32e40p/tb/uvmt",
    "DV_UVME_PATH": CVV + "/cv32e40p/env/uvme",
    "TBSRC_HOME": CVV + "/cv32e40p/tb",
    "DV_SVLIB_PATH": CVV + "/cv32e40p/vendor_lib/verilab",
}
# svlib's list is in core-v-verif and names the clone the makefiles make beside it
# (SVLIB_PKG), which is ivpm's packages/svlib.
CLONES = {CVV + "/cv32e40p/vendor_lib/verilab/svlib/": "../../packages/svlib/"}
for name, sub in [("UVML_HRTBT", "uvm_libs/uvml_hrtbt"), ("UVML_TRN", "uvm_libs/uvml_trn"),
                  ("UVML_LOGS", "uvm_libs/uvml_logs"), ("UVML_SB", "uvm_libs/uvml_sb"),
                  ("UVML_MEM", "uvm_libs/uvml_mem"),
                  ("UVMA_CORE_CNTRL", "uvm_agents/uvma_core_cntrl"),
                  ("UVMA_OBI_MEMORY", "uvm_agents/uvma_obi_memory"),
                  ("UVMA_RVFI", "uvm_agents/uvma_rvfi"), ("UVMA_CLKNRST", "uvm_agents/uvma_clknrst"),
                  ("UVMA_INTERRUPT", "uvm_agents/uvma_interrupt"),
                  ("UVMA_DEBUG", "uvm_agents/uvma_debug")]:
    VARS["DV_%s_PATH" % name] = CVV + "/lib/" + sub

HEADER = """\
// CORE-V-VERIF's CV32E40P UVM bench (uvmt_cv32e40p_tb), core-v-verif cv32e40p_v1.8.3 with
// the cv32e40p RTL and svlib its ExternalRepos.mk names (svcheck-plan P2.1; ivpm's `corpora`
// dep-set). Written by scripts/corpora/corev.py from the bench's .flist files: do not edit.
// Defines as the makefiles' default: CV32E40P_ASSERT_ON (uvmt.mk), NO_PULP (cfg/default.yaml),
// CV32E40P_TRACE_EXECUTION and CV32E40P_RVFI (dsim.mk, the flow with no ISS). UVM 1.2, as
// the simulators bundle. Read with -F: paths are relative to here.
// One compilation unit, as dsim compiles it: the tb uses the uvmt package's macros.
--single-unit
--top uvmt_cv32e40p_tb
+define+CV32E40P_ASSERT_ON
+define+NO_PULP
+define+CV32E40P_TRACE_EXECUTION
+define+CV32E40P_RVFI
// slang ranks solve-before in a foreach an error (§18.5.10); the interrupt agent has 2.
-Wno-nested-solve-before
+incdir+../../packages/uvm-1.2/src
../../packages/uvm-1.2/src/uvm_pkg.sv
"""


def expand(text):
    text = re.sub(r"\$\{(\w+)\}", lambda m: VARS[m.group(1)], text)
    for k, v in CLONES.items():
        text = text.replace(k, v)
    return text


def norm(path):
    return os.path.normpath(path)


def walk(path, out, seen):
    with open(os.path.join(CORPORA, path)) as f:
        for raw in f:
            line = raw.split("//")[0].strip()
            if not line:
                continue
            line = expand(line)
            if line.startswith("-f "):
                sub = norm(line[3:].strip())
                out.append("// " + sub)
                walk(sub, out, seen)
            elif line.startswith("+incdir+"):
                d = norm(line[len("+incdir+"):])
                if ("I", d) not in seen:
                    seen.add(("I", d))
                    out.append("+incdir+" + d)
            else:
                p = norm(line)
                if p not in seen:
                    seen.add(p)
                    out.append(p)


def main():
    out, seen = [], set()
    out.append("// " + VARS["DESIGN_RTL_DIR"].replace("/rtl", "") + "/cv32e40p_manifest.flist")
    walk(norm("../../packages/cv32e40p/cv32e40p_manifest.flist"), out, seen)
    out.append("// " + VARS["DV_UVMT_PATH"] + "/uvmt_cv32e40p.flist")
    walk(norm(VARS["DV_UVMT_PATH"] + "/uvmt_cv32e40p.flist"), out, seen)
    for p in out:
        if not p.startswith(("//", "+")) and not os.path.exists(os.path.join(CORPORA, p)):
            raise SystemExit("missing: %s (ivpm update -d default-dev,corpora)" % p)
    with open(OUT, "w") as f:
        f.write(HEADER + "\n".join(out) + "\n")
    print("wrote %s: %d files" % (os.path.relpath(OUT, ROOT),
                                  sum(1 for p in out if not p.startswith(("//", "+")))))


if __name__ == "__main__":
    main()
