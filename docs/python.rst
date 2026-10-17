Python API
==========

The ``svcheck`` package runs svcheck in-process. It needs no subprocess. The
wheel ships ``libsvcheck`` (the checker, slang and the model in one shared
library) and two extension modules on it:

``svcheck.sem``
   The curated model: every node kind of the :doc:`node reference
   <model/index>`, with its fields as ``getX()`` methods and its lists as
   ``x()`` (iterable, ``len()``) and ``getX()`` (a list).

``svcheck.core``
   The factory, ``Compilation`` and the command in-process.

Compiling
---------

``svcheck.compile(args)`` takes the command line's file arguments: ``-f``,
``-F``, ``+incdir+``, ``+define+``, ``--top``, ``--relax-lrm`` and
``--library-dir``. It accepts the lint options and ignores them. It returns a
``Compilation``:

.. code-block:: python

   import svcheck
   from svcheck import sem

   c = svcheck.compile(["-F", "tb.f", "--relax-lrm"])
   if c.design is None:          # slang reported errors
       print(c.diagnostics)
   for pkg in c.design.packages():
       for d in pkg.items():
           if isinstance(d, sem.Class) and not d.getInLibrary():
               base = d.getBase()
               print(pkg.getName(), d.getName(), base and base.getName())
               for m in d.members():
                   print("   ", type(m).__name__, m.getName())

A ``Compilation`` has these members:

``design``
   The model's root, an ``sem.Design``. It is None when the compilation has
   errors or the semantic layer is ``none``.

``errors``, ``diagnostics``
   How many errors slang reported, and their text as its command line prints
   them.

``files``
   The user files that were read, absolute and sorted. Library code is not
   listed.

``location(node)``
   Where a node is, as ``(path, line, column)``. ``file_path(fileid)`` is the
   lower-level form.

Library code (``uvm_pkg``, and anything under a ``--library-dir``) is lowered
as declarations only, so its subroutines have no body.

Bad arguments raise ``svcheck.core.CompileError``.

Every node keeps its compilation alive. A node you reach from ``design`` is
valid for as long as you hold it, even after the ``Compilation`` itself is
dropped.

Walking
-------

``sem.VisitorBase`` walks the tree a node owns. A subclass overrides the
``visitX`` methods it needs and calls ``super()`` to descend. Reference fields
are not followed: a class's ``base``, a call's ``subroutine`` and a
declaration's ``carrier`` point elsewhere in the model, so a walk visits each
node once.

.. code-block:: python

   class Calls(sem.VisitorBase):
       def __init__(self):
           super().__init__()
           self.names = []

       def visitCall(self, i):
           self.names.append(i.getName())
           super().visitCall(i)

   v = Calls()
   build_phase.accept(v)

Linting
-------

``svcheck.lint(args)`` runs the :doc:`svcheck command <cli>` in-process
with ``--format json``. It returns a ``LintResult`` with these fields:

- ``exit_status``: the command's exit status;
- ``report``: the parsed JSON, the same document ``svcheck ... --format json``
  prints;
- ``stderr``: what the command wrote to stderr.

.. code-block:: python

   r = svcheck.lint(["-F", "tb.f", "--preset", "lowrisc-dv"])
   for f in r.report["findings"]:
       print(f["code"], f["file"], f["line"], f["message"])

``svcheck.run_cli(args)`` runs any command line in-process, including the
query modes (``--explain``, ``--check-rules``, ``--list-markers``). It
returns ``(exit status, stdout, stderr)``.

The rule schema is installed once per process, by the first run or
compilation, with that call's ``--semantic`` layer. A later call that asks
for another layer still gets the first one.
