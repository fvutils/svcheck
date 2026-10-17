Running svcheck
###############

.. code-block:: sh

   svcheck --rules rules.yaml -f tb.f [--format text|json|sarif|github]

svcheck compiles the files of the command line as slang does (``-f`` and
``-F`` file lists, ``+incdir+``, ``+define+``, ``--top`` and every other slang
option), runs the rules over the compilation, and prints the findings. Code
under a ``--library-dir``, and UVM itself, is library code: it is compiled but
not checked.

Exit status: 0 no findings; 1 a finding of warning or error severity, or a
compilation with errors; 2 a rule file or configuration with errors, or bad
usage; 3 an internal error.

Which rules run
===============

svcheck's built-in packs (the UVM pack, ``SVCHK-UVM``) are always loaded, and
run as the profile and selectors say: none of them is in the default profile,
so ``--select SVCHK-UVM`` or ``--profile recommended`` asks for them. They read
the semantic model, so ``--semantic=none`` leaves them out. A rule file names
their shared helpers as ``uses: [uvm.yaml]``.

``--rules`` names a rule file, or a directory whose ``*.yaml`` rule files are
all loaded (files that only hold helpers for ``uses:`` are skipped). A rule of
a file named this way runs unless something narrows it:

``--profile NAME``
   ``minimal`` (the default), ``recommended``, ``library``, ``strict``, ``all``
   or ``none``. A profile enables the rules of its tiers among svcheck's own
   markers and packs; ``none`` turns everything off, the named files' rules
   too.
``--select SEL``, ``--ignore SEL``
   Repeatable. A selector is a rule's ID (``SVCHK-UVM001``) or name, an ID
   prefix (``SVCHK-UVM0``), a namespace (``SVCHK-UVM``, ``SLANG``), a tier
   (``suspicious``) or ``family:<name>``. The most specific selector wins; an
   ignore beats a select as specific.
``-Werror``, ``-Werror=SEL``, ``-Wno-error=SEL``, ``--no-warnings``
   Promote warnings to errors (all, or those a selector names; ``-Werror=info``
   promotes infos too), keep some as warnings, or drop warnings. Every other
   ``-W`` option is slang's. ``-Wno-error=X`` also reaches slang when ``X`` is one
   of its options or groups: it keeps an error slang lets be downgraded (such as
   ``range-width-oob``) a warning.

``--rules-for FILE...`` prints the rules each file gets, and ``--show-config``
the resolved selection, without compiling anything.

Configuration
=============

``--config FILE`` reads a YAML (or JSON) file. Paths and globs in it are
relative to its directory. The format is provisional: a TOML form and
discovery of a project file are to come.

.. code-block:: yaml

   svcheck:
     rules: [rules/]
     profile: recommended
     ignore: [style]
     severity: { SVCHK-UVM020: error }
     warnings: { error: [correctness] }
     rule:
       class-suffix: { driver: _drv }       # a rule's options
     overrides:                             # in order; a later one wins
       - files: ["vendor/**"]
         profile: none
       - context: agent                     # or "agent(apb)" for one unit
         select: [family:emulation]
     contexts:                              # declared contexts
       - files: ["third_party/**"]
         context: vip
         axis: provenance

An override applies to the files its ``files`` globs match and, with
``context``, to the code whose context has one of its labels (see
:doc:`rules/authoring`, Contexts); ``files`` and ``context`` together must
both match. A ``contexts`` entry declares a label for the packages, interfaces
and modules (and classes outside packages) in the files it names, or for the
packages whose names its ``packages`` globs match.

Waivers
=======

A comment waives findings on its line, or alone on a line, on the next line
that holds code:

.. code-block:: systemverilog

   phase.raise_objection(this);  // svcheck waive SVCHK-UVM040: drain watchdog
   // svcheck waive objection-in-agent, SLANG177: reset sequence, reviewed
   phase.raise_objection(this);

A waiver names rules by any selector and gives a reason. A waiver that waives
nothing (``SVCHK202``) or has no reason is itself reported. Waived findings
are counted, listed under ``waived`` in the JSON output, shown with
``--show-waived``, and written to SARIF as suppressed results.

slang's warnings
================

slang's own warnings are markers ``SLANG001`` … ``SLANG248``, named as their
slang options (``SLANG177`` is ``constant-conversion``). A warning in slang's
default set is in the ``suspicious`` tier, any other in ``pedantic``; neither
is in the ``minimal`` profile. Select them like rules (``--select SLANG``,
``--select constant-conversion``), waive them like rules, and svcheck asks
slang to report what is selected. slang's ``-Wno-<name>`` still silences one.
slang's errors are not markers: they are printed, and the rules that need the
compilation are then reported not run (``SVCHK231``). Warnings from slang's
analysis pass (``unused-variable`` and its group) are not collected yet.

Code that other tools accept
============================

slang follows the LRM strictly. Some testbenches rely on what simulators let
through; these slang options (``--relax-lrm`` turns on all of them) accept it:

``--allow-assertctl-string-scope``
   ``$assertoff(0, "tb.dut")``: a scope given as a string (VCS).
``--relax-package-exports``
   ``export pkg::*`` passes on every name of ``pkg``, not only those the
   exporting package references.
``--relax-virtual-method-types``
   An override's argument or return type that differs from the base's only
   in being ``bit`` rather than ``logic`` (same width and sign).
``--allow-typedef-class-for-interface-class``
   ``typedef class X;`` before ``interface class X``.
``--allow-virtual-iface-hier-ref``
   A virtual interface whose interface has an upward reference.
``--relax-array-element-conversions``
   Unpacked arrays assigned to each other, or copied out of an ``output``
   argument, whose elements differ only in being two- or four-state, or in sign.
``--relax-string-compare``
   A string compared with an integral operand, ``s[i] == t``: the integral
   operand is converted to a string.
``--relax-forward-method-refs``
   A class method that returns a value, called without parentheses before its
   declaration.
``--relax-logical-handle-operands``
   Class handles as operands of ``&&``, ``||``, ``->`` and ``<->`` (true when not
   null), as in a condition.
``--relax-dist-precedence``
   ``a == b dist {...}`` is ``(a == b) dist {...}``, as the grammar of a
   constraint's ``dist`` reads, not ``a == (b dist {...})`` (the precedence
   table's reading).
``--allow-hierarchical-const``
   slang's own: a hierarchical name in a constant, such as a bind's parameter
   value.

With ``--allow-virtual-iface-hier-ref``, names that a bound interface takes
from its bind target are not reported in the copy slang makes for a virtual
interface type; each real instance still checks them.

File lists written for a simulator often hold its own options. svcheck skips
the common ones of Verilator (``--cc``, ``--exe``, ``-o``, ``-Mdir``, …), VCS
(``-sverilog``, ``-full64``, ``-ntb_opts``, …) and Questa or Xcelium (``-sv``,
``-64bit``, ``-uvmhome``, …), and renames ``-top`` and ``--top-module`` to
``--top`` and ``-timescale`` to ``--timescale``. Verilator's ``--timing`` is
not skipped, since slang has a ``--timing`` of its own: add
``--cmd-ignore --timing,0``.

Queries
=======

``--list-markers``
   Every rule and marker svcheck knows: its own, slang's and the loaded rules'.
``--explain RULE``
   One rule or marker, by ID, name or old ID: its tier, what it matches,
   where it is defined, its options and its documentation.
``--check-rules``
   Checks the rule files and runs each rule's examples (``--uvm DIR`` gives
   UVM's ``src`` directory for the examples that need it). With no rule file,
   it checks the built-in packs.
``--contexts``
   Each unit the rules' classifiers find, its labels and their evidence.
``--statistics``, ``--stats``
   Findings per code; where the time and memory went.
