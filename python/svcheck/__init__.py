"""svcheck: a SystemVerilog checker whose rules are data.

The package ships the native ``svcheck`` executable (the console script runs
it) and, in-process (svcheck-plan P2.6b), libsvcheck and two extensions:

- ``svcheck.sem``: the curated SV model (generated from ``src/ast/*.yaml``);
- ``svcheck.core``: the factory, ``Compilation`` and the command in-process.

``compile()`` and ``lint()`` below are the short way in::

    c = svcheck.compile(["-f", "tb.f", "--relax-lrm"])
    for pkg in c.design.packages(): ...
    report = svcheck.lint(["-f", "tb.f", "--select", "SVCHK-UVM"]).report
"""
import json
from dataclasses import dataclass

from .__version__ import version as __version__
from .native import native_path

__all__ = ["__version__", "native_path", "compile", "lint", "run_cli", "LintResult"]


def compile(args):
    """Compiles and lowers what ``args`` names, as the svcheck command line
    does (``-f``, ``+incdir+``, ``--top``, ``--relax-lrm``, ``--library-dir``,
    ``--semantic``). Returns an ``svcheck.core.Compilation``; its ``design``
    is the model's root (None when slang reported errors: see ``errors`` and
    ``diagnostics``). Raises ``svcheck.core.CompileError`` on bad arguments."""
    from . import core
    return core.Factory.inst().compile(list(args))


def run_cli(args):
    """Runs the svcheck command in-process on ``args`` (no program name):
    (exit status, stdout, stderr), as the executable would give them."""
    from . import core
    return core.Factory.inst().run_cli(list(args))


@dataclass
class LintResult:
    """What ``lint()`` returns: the exit status, the command's JSON report
    (None when it wrote none: a rule-file or usage error), and its stderr."""
    exit_status: int
    report: dict
    stderr: str


def lint(args):
    """Lints in-process: the svcheck command on ``args`` with
    ``--format json``, so ``report`` is what ``svcheck ... --format json``
    prints."""
    rc, out, err = run_cli(list(args) + ["--format", "json"])
    report = json.loads(out) if out.strip() else None
    return LintResult(rc, report, err)
