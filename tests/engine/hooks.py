"""The rule engine's test hooks, through the native executable.

`svcheck --engine-test-hooks` serves LintTestHooks::dispatch (src/engine):
one JSON request per stdin line, one JSON reply per stdout line. EngineHooks
keeps one server process and has the method names and arguments of
pssparser's Cython LintTestHooks (python/core.pyx at 57c6b14), so the tests
copied from pssparser's tests/python/lintcore run against svcheck's fork
unchanged. Hooks that take a PSS tree (cel_eval_node, lint_run, ...) are
not here: they come back with the SV host (svcheck-plan P1.2).
"""
import json
import subprocess

from svcheck import native_path


class EngineHooks:
    def __init__(self):
        self._proc = subprocess.Popen(
            [native_path(), "--engine-test-hooks"],
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
