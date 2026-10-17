#!/usr/bin/env python3
"""Exports OpenTitan IP DV benches with FuseSoC and writes a file list for each
(svcheck-plan P2.1): packages/opentitan-dv/<ip>/ and tests/corpora/opentitan-<ip>.f.

    scripts/corpora/opentitan.py [IP ...]

The tree is ivpm's packages/opentitan (the `corpora` dep-set), and so are FuseSoC
and the Python packages its generators import. For each IP this runs the setup
step of `lowrisc:dv:<ip>_sim`'s `sim` target, as dvsim does for VCS (with its
`--mapping` to the generic prims), which runs
the generators (the RAL package, the CSR assertions) and exports every file the
target uses. The export's `src/` and `.eda.yml` are copied to
packages/opentitan-dv/<ip>, and the list is written from the `.eda.yml`: the
directory of each include file as `+incdir+`, every other file in order, the
toplevel as `--top`. It compiles as dvsim does: one unit, UVM 1.2, the `+define+`s
of the sim cfg (dvsim's common `build_opts`, the IP's and those of its first build
mode), and the sim cfg's `sim_tops` (the bind modules) as more `--top`s.

With no IP, the default set (IPS). P1.6b's opentitan-uart.f was written by hand
the same way, from the same commit, but without the mapping: it had the ASAP7 prims.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

import hjson
import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OT = os.path.join(ROOT, "packages", "opentitan")
OUT = os.path.join(ROOT, "packages", "opentitan-dv")
CORPORA = os.path.join(ROOT, "tests", "corpora")
VENV_BIN = os.path.join(ROOT, "packages", "python", "bin")
COMMIT = "83fc48ed3a"
# dvsim's flist flags (hw/dv/tools/dvsim/common_sim_cfg.hjson): the generic prims,
# not the first provider FuseSoC finds (prim_asap7, whose cells are not in the tree).
MAPPING = "lowrisc:prim_generic:all:0.1"

# IP -> the sim cfg, where it is not <ip>_sim_cfg.hjson (the variant dvsim runs
# by default, or the first of several).
IPS = {
    "uart": None, "i2c": None, "spi_host": None, "spi_device": "spi_device_2p_sim_cfg.hjson",
    "rv_timer": None, "aon_timer": None, "pattgen": None, "hmac": None,
    "aes": "aes_base_sim_cfg.hjson", "kmac": "kmac_base_sim_cfg.hjson", "keymgr": None,
    "csrng": None, "edn": "edn_edn0_sim_cfg.hjson",
    "entropy_src": "entropy_src_rng4bits_sim_cfg.hjson", "sysrst_ctrl": None,
    "adc_ctrl": None, "usbdev": None, "dma": None,
}

# Per-IP lines added to the list, each with why.
EXTRA = {
    "adc_ctrl": ["// slang ranks solve-before in a foreach an error (18.5.10); the env has 2.",
                 "-Wno-nested-solve-before"],
    # Errors slang lets be downgraded, kept as warnings (SLANG markers), since they
    # are worth a look (lrm-relaxations.md, W).
    "entropy_src": ["// current_rng_word[j*8 +: 8] on a bit[3:0] (xht agent): out of range.",
                    "-Wno-error=range-width-oob"],
    "keymgr": ["// force through a generate index that varies at run time (keymgr_if).",
               "-Wno-error=bad-procedural-force"],
    "sysrst_ctrl": ["// cross items through cfg.vif (slang's extension, which it ranks an",
                    "// error); solve-before in a foreach (18.5.10).",
                    "-Wno-error=nonstandard-hierarchical-cross",
                    "-Wno-error=nested-solve-before"],
}

HEADER = """\
// OpenTitan's {ip} DV ({core}, target sim), at opentitan {commit} (svcheck-plan P2.1;
// ivpm's `corpora` dep-set). Written by scripts/corpora/opentitan.py from FuseSoC's
// export in packages/opentitan-dv/{ip}: do not edit. Compiled as dvsim does: one
// unit, UVM 1.2, the sim cfg's +define+s (dvsim's common ones, the IP's, its first
// build mode's) and its sim_tops as extra tops. Read with -F: paths are relative to here.
--single-unit
{defines}// Developed on VCS and Xcelium, which accept what slang's relaxations accept
// (patches/slang SL-6...; docs/cli.rst).
--relax-lrm
// slang is right by IEEE 1800-2017 11.4.12.2; VCS and Xcelium accept a byte in a
// string concatenation, which OpenTitan's DV library has.
-Wno-string-int-concat
+incdir+../../packages/uvm-1.2/src
../../packages/uvm-1.2/src/uvm_pkg.sv
"""


def core_name(ip):
    with open(os.path.join(OT, "hw", "ip", ip, "dv", "%s_sim.core" % ip)) as f:
        m = re.search(r'^name:\s*"?([^"\s]+)"?', f.read(), re.M)
    return m.group(1)


def _load(path, seen):
    """The sim cfg at path merged with the IP cfgs it imports (dvsim concatenates
    lists): sim_tops, build_opts and build_modes. Of dvsim's own cfgs only
    common_sim_cfg.hjson is read, for its build_opts, not its imports (the tool
    cfgs, the test lists)."""
    out = {"sim_tops": [], "build_opts": [], "build_modes": [], "en_build_modes": [],
           "vars": {}}
    if path in seen:
        return out
    seen.add(path)
    with open(path) as f:
        c = hjson.load(f)
    common = os.path.basename(path) == "common_sim_cfg.hjson"
    if not common:
        for imp in c.get("import_cfgs", []):
            imp = imp.replace("{proj_root}", OT).replace("{dv_root}", os.path.join(OT, "hw", "dv"))
            if "/hw/dv/tools/dvsim/" in imp and not imp.endswith("/common_sim_cfg.hjson"):
                continue
            sub = _load(imp, seen)
            for k in ("sim_tops", "build_opts", "build_modes", "en_build_modes"):
                out[k] += sub[k]
            out["vars"].update(sub["vars"])
    for k in ("sim_tops", "build_opts"):
        out[k] += c.get(k, [])
    if not common:
        out["build_modes"] += c.get("build_modes", [])
        out["en_build_modes"] += c.get("en_build_modes", [])
    out["vars"].update({k: v for k, v in c.items() if isinstance(v, (str, int))})
    return out


def sim_cfg(ip, cfg):
    """dvsim's build for the IP: its extra tops (sim_tops: the bind modules, each a
    -top) and its +define+s (the common ones, the IP's, the modes every build enables,
    then the first other build mode's: dvsim builds one mode per variant, and the
    first will do)."""
    c = _load(os.path.join(OT, "hw", "ip", ip, "dv", cfg or "%s_sim_cfg.hjson" % ip), set())
    vs = dict(c["vars"], tool="vcs")

    def subst(o):
        for _ in range(4):
            o = re.sub(r"\{(\w+)\}", lambda m: str(vs.get(m.group(1), m.group(0))), o)
        return o

    enabled = {subst(m) for m in c["en_build_modes"]}
    opts = list(c["build_opts"])
    for m in c["build_modes"]:
        if m["name"] in enabled:
            opts += m.get("build_opts", [])
    # A mode enabled for some tool ({tool}_...) is not a variant either.
    per_tool = [re.compile(re.escape(m).replace(r"\{tool\}", r"\w+") + "$")
                for m in c["en_build_modes"]]
    variants = [m for m in c["build_modes"]
                if not any(r.match(m["name"]) for r in per_tool)]
    if variants:
        opts += variants[0].get("build_opts", [])
    tops = []
    for t in map(subst, c["sim_tops"]):
        if t != vs.get("tb") and t not in tops:
            tops.append(t)
    defs = []
    for o in map(subst, opts):
        if o.startswith("+define+") and o not in defs:
            defs.append(o)
    return tops, defs


def export(ip, core):
    env = dict(os.environ, PATH=VENV_BIN + os.pathsep + os.environ.get("PATH", ""))
    with tempfile.TemporaryDirectory() as build:
        p = subprocess.run([os.path.join(VENV_BIN, "fusesoc"), "--cores-root", OT, "run",
                            "--target=sim", "--setup", "--build-root=" + build, "--tool=vcs",
                            "--mapping=" + MAPPING, core], capture_output=True, text=True, env=env)
        if p.returncode != 0:
            raise RuntimeError("fusesoc failed for %s:\n%s%s" % (core, p.stdout, p.stderr))
        work = os.path.join(build, core.replace(":", "_"), "sim-vcs")
        dest = os.path.join(OUT, ip)
        shutil.rmtree(dest, ignore_errors=True)
        os.makedirs(dest)
        shutil.copytree(os.path.join(work, "src"), os.path.join(dest, "src"))
        eda = core.replace(":", "_") + ".eda.yml"
        shutil.copy(os.path.join(work, eda), dest)
        return os.path.join(dest, eda)


def write_list(ip, core, eda, tops, defs):
    with open(eda) as f:
        d = yaml.safe_load(f)
    base = os.path.relpath(os.path.dirname(eda), CORPORA)
    lines, incdirs = [], set()
    for f in d["files"]:
        if f.get("file_type") != "systemVerilogSource":
            continue
        path = os.path.join(base, f["name"])
        if f.get("is_include_file"):
            inc = os.path.dirname(path)
            if inc not in incdirs:
                incdirs.add(inc)
                lines.append("+incdir+" + inc)
        else:
            lines.append(path)
    out = HEADER.format(ip=ip, core=core, commit=COMMIT,
                        defines="".join(o + "\n" for o in defs))
    out += "".join(o + "\n" for o in EXTRA.get(ip, []))
    out += "\n".join(lines) + "\n--top %s\n" % d["toplevel"]
    out += "".join("--top %s\n" % t for t in tops)
    path = os.path.join(CORPORA, "opentitan-%s.f" % ip)
    with open(path, "w") as f:
        f.write(out)
    return path, sum(1 for l in lines if not l.startswith("+"))


def main():
    ips = sys.argv[1:] or list(IPS)
    failed = []
    for ip in ips:
        core = core_name(ip)
        try:
            eda = export(ip, core)
        except RuntimeError as e:
            failed.append(ip)
            print(e, file=sys.stderr)
            continue
        tops, defs = sim_cfg(ip, IPS.get(ip))
        path, n = write_list(ip, core, eda, tops, defs)
        print("%s: %d files" % (os.path.relpath(path, ROOT), n))
    if failed:
        sys.exit("export failed: " + " ".join(failed))


if __name__ == "__main__":
    main()
