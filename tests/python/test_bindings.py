"""The Python bindings, in-process (svcheck-plan P2.6b).

- ``svcheck.compile()`` loads a corpus's ``-f`` and gives the model:
  classes, their members and base classes, and the calls in their bodies,
  walked with the generated ``sem.VisitorBase``.
- A node keeps its compilation alive (``_owner``), so it outlives the
  ``Compilation`` object and the ``Design`` it was reached from.
- ``svcheck.lint()`` returns what the executable's ``--format json`` prints.
- Errors: bad arguments raise ``CompileError``; slang's errors leave no
  model and are in ``diagnostics``; the command's own are in its stderr.
"""
import gc
import json
import os
import subprocess

import pytest

import svcheck
from svcheck import core, native_path, sem

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
UVM = os.path.join(ROOT, "packages", "uvm-core", "src")
CORPUS = os.path.join(ROOT, "tests", "corpora", "ctx-positive.f")
uvm = pytest.mark.skipif(not os.path.isfile(os.path.join(UVM, "uvm_pkg.sv")),
                         reason="UVM is not in packages/ (ivpm update)")


class Calls(sem.VisitorBase):
    """The calls under a node, by name."""

    def __init__(self):
        super().__init__()
        self.names = []

    def visitCall(self, i):
        self.names.append(i.getName())
        super().visitCall(i)


def classes(design, package):
    for p in design.packages():
        if p.getName() == package:
            return {d.getName(): d for d in p.items() if isinstance(d, sem.Class)}
    raise AssertionError("no package %s" % package)


@pytest.fixture(scope="module")
def corpus():
    return svcheck.compile(["-F", CORPUS])


def test_factory():
    f = core.Factory.inst()
    assert f is core.Factory.inst()
    assert f.version == svcheck.__version__


@uvm
def test_walk_corpus(corpus):
    assert corpus.errors == 0, corpus.diagnostics
    assert any(f.endswith("tb_pkg.sv") for f in corpus.files)
    cls = classes(corpus.design, "tb_pkg")
    assert {"apb_item", "apb_seq", "apb_driver"} <= set(cls)

    drv = cls["apb_driver"]
    assert drv.getBase().getName() == "uvm_driver"
    assert drv.getBase().getInLibrary()
    assert not drv.getInLibrary()
    members = {m.getName(): m for m in drv.members()}
    assert isinstance(members["vif"], sem.Variable)
    assert isinstance(members["build_phase"], sem.Subroutine)

    v = Calls()
    members["build_phase"].accept(v)
    assert "get" in v.names and "build_phase" in v.names

    path, line, col = corpus.location(drv)
    assert path.endswith("tb_pkg.sv") and line > 0 and col > 0

    # Library code is lowered as declarations only.
    lib = classes(corpus.design, "uvm_pkg")["uvm_component"]
    assert lib.getInLibrary()
    bodies = [m.getBody() for m in lib.members() if isinstance(m, sem.Subroutine)]
    assert bodies and all(b is None for b in bodies)


@uvm
def test_node_keeps_compilation():
    c = svcheck.compile(["-F", CORPUS])
    seq = classes(c.design, "tb_pkg")["apb_seq"]
    del c
    gc.collect()
    body = {m.getName(): m for m in seq.members()}["body"]
    v = Calls()
    body.accept(v)
    assert {"create", "start_item", "finish_item"} <= set(v.names)


@uvm
def test_lint_is_the_cli():
    args = ["-F", CORPUS, "--select", "SVCHK-UVM"]
    res = svcheck.lint(args)
    exe = subprocess.run([native_path()] + args + ["--format", "json"],
                         capture_output=True, text=True, timeout=300)
    assert res.exit_status == exe.returncode
    assert res.report == json.loads(exe.stdout)
    assert {f["code"] for f in res.report["findings"]} >= {"SVCHK-UVM037", "SVCHK-UVM038"}


def test_compile_errors(tmp_path):
    with pytest.raises(core.CompileError, match="no-such-option"):
        svcheck.compile(["--no-such-option"])

    bad = tmp_path / "bad.sv"
    bad.write_text("module m; int x = ; endmodule\n")
    c = svcheck.compile([str(bad)])
    assert c.errors > 0
    assert c.design is None
    assert "bad.sv" in c.diagnostics


def test_run_cli_captures():
    rc, out, err = svcheck.run_cli(["--format", "nope", "x.sv"])
    assert rc == 2 and out == "" and "unknown --format 'nope'" in err
    rc, out, err = svcheck.run_cli(["--version"])
    assert rc == 0 and out.startswith("svcheck ")
