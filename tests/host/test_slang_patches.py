"""svcheck's local slang patches (patches/slang/README.md): each issue's repro,
compiled by the shipped binary with slang's defaults (instance caching on).
A failure here after a slang bump means a patch no longer applies or no
longer fixes its issue.
"""
import os
import subprocess

import pytest

from svcheck import native_path

HERE = os.path.dirname(os.path.abspath(__file__))
REPROS = os.path.join(HERE, "slang")


@pytest.mark.parametrize("repro, args", [
    # SL-1: false "cyclic dependency" on a cached duplicate instance's interface.
    ("sl1-param-cycle.sv", ["--top", "top"]),
    # SL-2: an inherited property as a randomize() argument.
    ("sl2-randomize-inherited.sv", []),
    # SL-3: two adjacent macro pastes (`a````b`).
    ("sl3-adjacent-pastes.sv", []),
    # SL-4: a covergroup sample() argument's default names a class member.
    ("sl4-covergroup-sample-default.sv", []),
    # SL-5: parameter-dependent errors from a generic body checked unset.
    ("sl5-generic-body-unset.sv", ["--top", "top"]),
    # SL-16: sample() arguments and bins named like coverage built-ins (start, stop).
    ("sl16-coverage-builtin-names.sv", ["--top", "tb"]),
    # SL-17: binsof of a hierarchical cross item (slang's extension, an error by default).
    ("sl17-binsof-hier-cross-item.sv", ["--top", "tb", "-Wno-nonstandard-hierarchical-cross"]),
])
def test_repro_compiles(repro, args):
    r = subprocess.run([native_path(), os.path.join(REPROS, repro)] + args,
                       capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, r.stderr
    assert "error" not in r.stderr, r.stderr


# SL-6..SL-15 (svcheck-plan P2.3, P2.4b): relaxations of LRM-strict errors, each
# behind a slang option that svcheck passes through; --relax-lrm turns on all of them.
RELAX = [
    ("sl6-assertctl-string-scope.sv", "--allow-assertctl-string-scope"),
    ("sl7-export-star.sv", "--relax-package-exports"),
    ("sl8-override-bit-logic.sv", "--relax-virtual-method-types"),
    ("sl9-typedef-iface-class.sv", "--allow-typedef-class-for-interface-class"),
    ("sl10-vif-upward-ref.sv", "--allow-virtual-iface-hier-ref"),
    ("sl10b-vif-bind-target-ref.sv", "--allow-virtual-iface-hier-ref"),
    ("sl11-queue-logic-bit.sv", "--relax-array-element-conversions"),
    ("sl11c-output-queue.sv", "--relax-array-element-conversions"),
    ("sl12-string-compare.sv", "--relax-string-compare"),
    ("sl13-forward-method-ref.sv", "--relax-forward-method-refs"),
    ("sl14-logical-handles.sv", "--relax-logical-handle-operands"),
    ("sl15-dist-precedence.sv", "--relax-dist-precedence"),
]


def _compile(repro, *args):
    return subprocess.run([native_path(), os.path.join(REPROS, repro), "--top", "tb"]
                          + list(args), capture_output=True, text=True, timeout=60)


@pytest.mark.parametrize("repro, flag", RELAX)
def test_relaxation_is_off_by_default(repro, flag):
    r = _compile(repro)
    assert r.returncode != 0 and "error" in r.stderr, r.stderr


@pytest.mark.parametrize("repro, flag", RELAX)
@pytest.mark.parametrize("how", ["flag", "relax-lrm"])
def test_relaxation_accepts_the_repro(repro, flag, how):
    r = _compile(repro, flag if how == "flag" else "--relax-lrm")
    assert r.returncode == 0, r.stderr
    assert "error" not in r.stderr, r.stderr


@pytest.mark.parametrize("repro, errors", [
    # Chained re-exports and a cycle resolve; an unknown name is still an error.
    ("sl7b-export-chains.sv", ["use of undeclared identifier 'nope'"]),
    # A width or sign change, and an enum against a vector, stay errors.
    ("sl8b-override-still-strict.sv", None),
    # Only `typedef class` for an interface class, not the reverse.
    ("sl9b-reverse-still-strict.sv", None),
])
def test_relaxation_stays_narrow(repro, errors):
    r = _compile(repro, "--relax-lrm")
    assert r.returncode != 0, r.stderr
    for e in errors or []:
        assert e in r.stderr, r.stderr


def test_relaxed_array_forms():
    # Fixed, dynamic and queue arrays mix; a width change is still an error.
    r = _compile("sl11b-array-forms.sv", "--relax-lrm")
    assert r.returncode != 0
    assert r.stderr.count("error:") == 1, r.stderr
