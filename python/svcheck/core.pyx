# cython: language_level=3
"""svcheck in-process (svcheck-plan P2.6b): libsvcheck, reached through its
``svcheck_getFactory()`` entry point.

The extension links neither libsvcheck nor anything in it. The library is
loaded with ctypes, from the same place the generated ``svcheck.sem`` loads
it (a checkout's ``build/lib``, else this package), so the two modules share
one copy of it. A ``Compilation`` owns slang's compilation and the model;
every ``svcheck.sem`` node reached from its ``design`` keeps it alive.
"""
import ctypes
import os
import sys
from libc.stdint cimport intptr_t
from libcpp.string cimport string as std_string
from libcpp.vector cimport vector as std_vector
from svcheck cimport core_decl
from svcheck cimport sem
from svcheck cimport sem_decl
from svcheck import sem as _sem

if sys.platform == "darwin":
    _LIBNAME = "libsvcheck.dylib"
elif sys.platform == "win32":
    _LIBNAME = "svcheck.dll"
else:
    _LIBNAME = "libsvcheck.so"


def library_path():
    """libsvcheck: a checkout's build tree first (as ``svcheck.sem`` looks),
    else the copy in this package."""
    pkg_dir = os.path.dirname(os.path.abspath(__file__))
    build_dir = os.path.abspath(os.path.join(pkg_dir, "..", "..", "build"))
    for libdir in ("lib", "lib64", "bin"):
        p = os.path.join(build_dir, libdir, _LIBNAME)
        if os.path.isfile(p):
            return p
    p = os.path.join(pkg_dir, _LIBNAME)
    if not os.path.isfile(p):
        raise ImportError("svcheck: %s not found (looked in %s and %s)"
                          % (_LIBNAME, build_dir, pkg_dir))
    return p


cdef std_vector[std_string] _args(args):
    cdef std_vector[std_string] ret
    if isinstance(args, (str, bytes)):
        raise TypeError("svcheck: args is a list of arguments, not one string")
    for a in args:
        ret.push_back(os.fspath(a).encode())
    return ret


cdef Factory _inst = None

cdef class Factory(object):
    """libsvcheck's factory: one per process."""

    @staticmethod
    def inst():
        global _inst
        cdef Factory f
        if _inst is None:
            # Through svcheck.sem first: it loads the same file.
            _sem.Factory.inst()
            so = ctypes.cdll.LoadLibrary(library_path())
            fn = so.svcheck_getFactory
            fn.restype = ctypes.c_void_p
            f = Factory()
            f._hndl = <core_decl.IFactoryP>(<intptr_t>fn())
            _inst = f
        return _inst

    @property
    def version(self):
        return self._hndl.getVersion().decode()

    def run_cli(self, args):
        """Runs the svcheck command on ``args`` (no program name):
        (exit status, stdout, stderr)."""
        cdef std_string out, err
        cdef std_vector[std_string] a = _args(args)
        cdef int rc = self._hndl.runCli(a, out, err)
        return rc, out.decode(errors="replace"), err.decode(errors="replace")

    def compile(self, args):
        """Compiles and lowers what ``args`` names, as the command line does.
        Raises ``CompileError`` when the arguments are wrong."""
        cdef std_string err
        cdef std_vector[std_string] a = _args(args)
        cdef core_decl.ICompilation *c = self._hndl.compile(a, err)
        if c == NULL:
            raise CompileError(err.decode(errors="replace").strip())
        ret = Compilation()
        ret._hndl = c
        return ret


class CompileError(Exception):
    """The arguments were wrong, or named nothing to compile."""


cdef class Compilation(object):
    """A compiled design: slang's compilation and the model lowered from it."""

    def __dealloc__(self):
        if self._hndl != NULL:
            del self._hndl
            self._hndl = NULL

    @property
    def design(self):
        """The model's root, an ``svcheck.sem.Design``; None when the
        compilation has errors or the semantic layer is ``none``."""
        cdef sem_decl.IDesign *d
        cdef sem.Design ret
        d = self._hndl.getDesign()
        if d == NULL:
            return None
        ret = sem.Design.mk(d, False)
        ret._owner = self
        return ret

    @property
    def errors(self):
        """How many errors slang reported."""
        return self._hndl.getErrors()

    @property
    def diagnostics(self):
        """slang's errors, as its command line prints them."""
        return self._hndl.getDiagnostics().decode(errors="replace")

    @property
    def files(self):
        """The user files read, absolute and sorted; not library code."""
        cdef const std_vector[std_string] *fs = &self._hndl.getFiles()
        return [fs.at(i).decode() for i in range(fs.size())]

    def file_path(self, fileid):
        """The path of a location's ``fileid``; "" for none."""
        return self._hndl.getFilePath(fileid).decode()

    def location(self, node):
        """Where a node is: (path, line, column); None for nowhere."""
        loc = node.getLocation()
        if loc.fileid < 0:
            return None
        return self.file_path(loc.fileid), loc.lineno, loc.linepos
