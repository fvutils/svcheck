"""The rule engine's test hooks, through the native executable.

`svcheck --engine-test-hooks` serves LintTestHooks::dispatch (src/engine):
one JSON request per stdin line, one JSON reply per stdout line. EngineHooks
keeps one server process and has the method names and arguments of
pssparser's Cython LintTestHooks (python/core.pyx at 57c6b14), so the tests
copied from pssparser's tests/python/lintcore run against svcheck's fork
unchanged. The hooks that take a tree (cel_eval_node, lint_run, ...) take
SV source text in its place, which the SV host parses alone with slang, as
`snippet.sv` (svcheck-plan P1.2); `node` is its CompilationUnit.
"""
import json
import subprocess

from svcheck import native_path


class EngineHooks:
    def __init__(self, semantic=None, uvm=None):
        """`semantic`: "model" or "slang" for a semantic layer, whose node hooks
        compile their source (svcheck-plan P1.3), against uvm_pkg from `uvm`
        (UVM's src directory) when the source mentions `uvm_`."""
        args = [native_path(), "--engine-test-hooks"]
        if semantic:
            args.append("--semantic=" + semantic)
        if uvm:
            args.append("--uvm=" + uvm)
        self._proc = subprocess.Popen(
            args,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True,
            encoding="utf-8", errors="surrogateescape", bufsize=1)

    def close(self):
        if self._proc.poll() is None:
            self._proc.stdin.close()
            self._proc.wait(timeout=30)

    def _call(self, op, *args):
        self._proc.stdin.write(json.dumps({"op": op, "args": list(args)}) + "\n")
        self._proc.stdin.flush()
        line = self._proc.stdout.readline()
        if not line:
            raise RuntimeError("svcheck --engine-test-hooks exited (rc=%s) during %s"
                               % (self._proc.poll(), op))
        return json.loads(line)

    @staticmethod
    def _doc(doc):
        return doc if isinstance(doc, str) else json.dumps(doc)

    @staticmethod
    def _bindings(bindings):
        return json.dumps(bindings) if bindings else ""

    def yaml_load(self, name, text):
        return self._call("yamlLoad", name, text)

    def yaml_source_maps(self, name, text):
        return self._call("yamlSourceMaps", name, text)

    def yaml_cel_parse(self, name, text, key):
        return self._call("yamlCelParse", name, text, key)

    def cel_parse(self, text):
        return self._call("celParse", text)

    def cel_eval(self, text, max_steps=0, bindings=None):
        return self._call("celEval", text, self._bindings(bindings), max_steps)

    def cel_functions(self):
        return self._call("celFunctions")

    def cel_check(self, doc, name="check.yaml"):
        return self._call("celCheck", name, self._doc(doc))

    def rule_load(self, name, text, builtin=False):
        return self._call("ruleLoad", name, text, bool(builtin))

    def rules_schema(self):
        return self._call("rulesSchema")

    def regex_search(self, pattern, text):
        return self._call("regexSearch", pattern, text)

    def reflect_schema(self):
        return self._call("reflectSchema")

    def cel_eval_node(self, text, source, max_steps=0, bindings=None):
        return self._call("celEvalNode", source, text, self._bindings(bindings), max_steps)

    def cel_check_node(self, doc, source, name="check.yaml"):
        return self._call("celCheckNode", source, name, self._doc(doc))

    def lint_walk(self, name, text, source):
        return self._call("lintWalk", source, name, text)

    def lint_run(self, name, text, source, enabled=None, max_steps=0, gate_errors=0):
        return self._call("lintRun", source, name, text, enabled is None,
                          list(enabled or []), max_steps, gate_errors)

    def lint_examples(self, name, text, rules=None):
        """The examples of rule file `name`'s rules (those in `rules`, or all),
        run as src/engine/run/Examples.h does; a result per example."""
        return self._call("lintExamples", name, text, rules is None, list(rules or []))

    def mk_lint_engine(self):
        """A whole LintEngine in the server (P1.5), as pssparser's
        `factory.mkLintEngine()`."""
        return EngineProxy(self, self._call("engineNew")["id"])


class EngineProxy:
    """A LintEngine held by the hooks server, with the method names of
    pssparser's Cython LintEngine (python/core.pyx)."""

    def __init__(self, hooks, ident):
        self._hooks = hooks
        self._id = ident

    @property
    def rule_format_version(self):
        return self._hooks._call("engineInfo", self._id)["format_version"]

    def num_rule_sets(self):
        return self._hooks._call("engineInfo", self._id)["rule_sets"]

    def load_rules(self, name, text, builtin=False):
        return self._hooks._call("engineLoadRules", self._id, name, text, bool(builtin))["loaded"]

    def rules(self):
        return self._hooks._call("engineRules", self._id)

    def rule_diagnostics(self):
        return self._hooks._call("engineDiagnostics", self._id)

    def run(self, source, request=None):
        """LintEngine::run over `source` (SV text), with `request`."""
        return self._hooks._call("engineRun", self._id, source, json.dumps(request or {}))


def check_examples(hooks, name, text, rules=None):
    """Runs the examples of rule file `name`, raising AssertionError that lists
    every failing one (and every load diagnostic); returns the results."""
    r = hooks.lint_examples(name, text, rules)
    assert r["ok"], r
    errors = [d for d in r["diagnostics"] if d["severity"] == "error"]
    assert r["loaded"] and not errors, errors
    failed = [x["describe"] for x in r["results"] if not x["ok"]]
    if failed:
        raise AssertionError("\n".join(failed))
    return r["results"]
