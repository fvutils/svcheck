#####################
Writing rules
#####################

A rule file declares checks in YAML, with their conditions written as
expressions in a subset of `CEL <https://github.com/google/cel-spec>`_. The
format is pssparser's ``pss-rules`` under the root key ``sv-rules``; this page
is about what is particular to SystemVerilog: which nodes a rule can match,
what it can read from them, and the traps that rule authors have met.

A first rule
============

.. code-block:: yaml

   # acme.yaml
   sv-rules:
     version: 1
     rules:
       - id: ACME001
         name: unchecked-randomize
         tier: suspicious
         summary: The result of randomize() is ignored
         message: randomize() result is discarded; a constraint failure goes unnoticed
         match: { kind: Call }
         let:
           p: parent(node)
         where: >-
           node.name == "randomize" && !in_library(node) &&
           (p.isa(ExprStmt) || (p.isa(Conversion) && p.type.name == "void"))
         examples:
           - code: |
               class item; rand int x; endclass
               class gen;
                 task t(item it);
                   it.randomize();                  // expect: ACME001
                   if (!it.randomize()) $fatal(1, "randomize");
                 endtask
               endclass

.. code-block:: console

   $ svcheck --rules acme.yaml -f tb.f
   tb/gen.sv:4:5: warning: randomize() result is discarded; a constraint failure goes unnoticed [ACME001]
   svcheck: 0 errors, 1 warning

``svcheck`` takes slang's own source arguments (``-f``/``-F`` file lists,
``+incdir+``, ``+define+``, ``--single-unit``, ``--top``), ``--rules`` as
often as needed, ``--format text|json|sarif`` and ``--library-dir DIR`` (code
under it is library code: not checked, and ``in_library()``). ``uvm_pkg`` is
always library code.

A rule's keys are pss-rules': ``id`` (a namespace of 2--5 capitals, an
optional ``-TEAM`` qualifier, three digits; ``SVCHK`` is svcheck's own),
``name``, ``tier``, ``summary``, ``message`` (with ``${...}`` interpolation),
``doc``, ``match``, ``let``, ``where``, ``options``, ``define`` and
``examples``. A file may also have ``defaults:``, ``define:`` (helpers shared
by its rules), ``options:`` (read by every rule and helper as
``options.<name>``), ``uses:`` (helper files, relative to the file naming
them; a bare name with no such file beside it is a built-in helper file, as
``uses: [uvm.yaml]`` for the UVM pack's vocabulary: roles such as
``is_component(c)`` and ``is_sequence(c)``, ``PHASES``, ``config_db(k)``, and
its content classifiers on the ``unit`` axis) and, experimentally,
``contexts:`` and ``applies_to:``.

What a rule can match
=====================

A rule matches node *kinds* of two layers, which one run sees together:

**slang's syntax trees.** Every file as parsed, with macros expanded. A rule
on syntax runs on each file that parsed, even when the design does not
elaborate, so it suits checks of how code is written. The kinds are slang's
own (``scripts/syntax.txt`` in slang's sources), in two forms:

* a **syntax kind**, ``ClassDeclaration``, ``AddExpression``: exactly one kind;
* a **syntax type**, the C++ class name ending in ``Syntax``:
  ``ClassDeclarationSyntax``, ``BinaryExpressionSyntax``. It matches every
  kind of that type: ``BinaryExpressionSyntax`` is ``AddExpression``,
  ``SubtractExpression`` and the other binary operators.

The fields are slang's, by the names ``syntax.txt`` gives them. A token is a
``Token`` value (``text``, ``raw``, ``kind``, ``missing``, ``line``,
``col``), so a rule writes ``node.name.text``. **A token the source does not
have is null**, not an empty token: ``node.uniqueOrPriority`` is null for a
plain ``case``, so test it (``node.uniqueOrPriority != null &&
node.uniqueOrPriority.text == "unique"``) or read it with ``.?``.

**The curated model** (:doc:`../model/index`). The elaborated design,
lowered to a small set of kinds: ``Package``, ``Definition``, ``Class``,
``Subroutine``, ``Variable``, ``Call``, ``Assign``, ``Timed`` and so on, with
names resolved (``NamedValue.decl``, ``Call.subroutine``), types
(``Expr.type``, a ``TypeRef``) and constants (``const_int``,
``const_string``). A model rule needs a design that compiles; where it does
not, each model rule is reported once as not run (``SVCHK231``) and syntax
rules still run. The model's class names never collide with syntax kinds:
``Class`` is the model's, ``ClassDeclaration`` the syntax's.

Write a rule on the model when it is about what the code *means* (a class
derives from ``uvm_object``, a call resolves to ``uvm_config_db::get``), and
on syntax when it is about how the code is *written* (a keyword, a layout,
what a macro was given).

The model, for rule authors
===========================

These are the parts of the model that have surprised rule authors so far.

Library code is declarations only
   ``uvm_pkg`` and anything under ``--library-dir`` is lowered without
   subroutine bodies: its classes, members, signatures and types are there,
   its statements are not. ``in_library(n)`` is true for it and for anything
   inside it. A rule on calls or statements finds only user code anyway;
   a rule on declarations should say ``!node.inLibrary``.

One node per declaration
   A module or interface instantiated many times is one ``Definition``, its
   items lowered from its first instance, so a rule reports each problem
   once. **Generate blocks** are ``GenBlock`` nodes in their definition's
   ``items``: each branch of a generate ``if``/``case`` and each ``for`` body
   once, from the first instance that elaborates it, whatever the parameters
   of the others. A branch that no instance elaborates is there with
   ``isElaborated`` false and no items.

Generic classes need a bench (S-2)
   A parameterised class is one ``Class``, its *generic body*; every
   specialisation refers to it. The body is lowered from a specialisation
   the design makes (the first by type name when there are several), and
   from the class's defaults only when the design makes none. So code that
   depends on a type parameter, such as ``vif.drive(...)`` through
   ``type vif_t = int``, is understood only when the compilation includes a
   bench that specialises the class. Checking a VIP alone, with no bench,
   gives its declarations, registrations and members in full, but its calls
   through type parameters may not resolve.

Processes
   ``initial``, ``final`` and the ``always`` forms are ``Process`` nodes in
   their module, interface or program, with their bodies. ``run_test()`` in a
   bench's ``initial`` block is a ``Call`` under a ``Process``.

Bodies are blocks
   A ``Subroutine.body`` is always a ``Block``, even with one statement, so
   ``s.body.stmts[0]`` is its first statement. A ``fork`` is a ``Block``
   whose ``kind`` is a join (``BlockKind.JoinAny``); a named block has its
   ``label``.

What waits
   Every statement that can let time pass is a ``Timed``: a delay or event
   control (``#5``, ``@(posedge clk)``), ``wait``, ``wait fork`` and
   ``wait_order``, told apart by ``kind``. ``exprs[0]`` is a delay's value:
   ``#0`` has ``const_int`` 0, and a time literal (``#0ns``) has
   ``constKind == ConstKind.Real`` and ``constReal``. Calling a task can
   also let time pass; the model does not follow calls.

Arguments
   A call's ``args`` are in the order of the subroutine's formals, and an
   argument the call leaves out is there too, filled in from the formal's
   default, with ``isDefault`` set (its location is the default's, in the
   subroutine's declaration). So "``raise_objection`` was given a
   description" is ``!call.args[1].isDefault``. A formal argument is a
   ``Variable`` with a ``direction``; its default value is its ``init``.

``this``, ``super`` and ``void'``
   ``this`` is a ``NamedValue`` named ``"this"`` (``decl`` is null).
   Passing it where a base class is expected wraps it in an implicit
   ``Conversion``: ``create("x", this)`` has ``args[1].operand.name ==
   "this"``. ``super.f()`` is a ``Call`` with ``isSuper``; ``super.new()`` is
   a ``NewClass`` with ``isSuper``, which constructs nothing (its type is
   ``void``). A statement ``void'(f());`` is an ``ExprStmt`` with
   ``isVoidCast``: slang keeps no cast for it, so the ``ExprStmt``'s
   ``expr`` is the call. A ``void'`` inside an expression is a
   ``Conversion`` whose type is ``void``.

Types
   ``type_of(e)`` is a ``TypeRef``. Compare classes, not names: a
   specialisation prints with its arguments and without its package
   (``uvm_config_db#(int)``), so test ``t.classDecl.qname ==
   "uvm_pkg::uvm_config_db"``, or ``derives_from(t, "uvm_pkg::uvm_object")``.
   ``t.args`` are a specialisation's type arguments.

Accesses
   ``a.b.c[i]`` is a chain of ``MemberAccess`` and selects;
   ``access_path(e)`` lists it from the root, and ``access_root(e)`` is the
   root. "Writes through a virtual interface, however it is reached" is
   ``access_path(a.lhs).exists(p, p.type.isVirtualInterface)``.

Order of statements
   ``before(n)`` and ``after(n)`` are the statements that run before and
   after the one holding ``n``, straight-line: in its block and the blocks
   around it, up to the subroutine or process, with no branch merging. A
   fork's statements are not before or after each other. They answer
   "``disable fork`` right after a ``join_any``" or "code after
   ``run_test()``", not questions about paths.

Class hierarchy
   ``c.base`` is the class extended, ``c.supers`` every ancestor nearest
   first, ``c.interfaces`` every interface class implemented, however.
   ``derives_from(c, name)`` follows both. ``subclasses(c)`` goes the other
   way: the classes whose base is ``c``. ``member(c, name)`` finds a member,
   inherited too; ``c.members`` are the class's own.

Macros
   The model and the syntax trees see what macros expand to. ``macro_of(n)``
   is the macro whose expansion made a node (the outermost when they nest),
   or ``""``: a rule about code as written says ``macro_of(node) == ""``,
   and a rule may check what a macro produced (``uvm_object_utils`` makes a
   ``type_id`` typedef) rather than the macro's name. Findings are reported
   where the macro is used.

Narrowing
=========

The checker types every expression, and a field read must exist on the
class it knows. ``node.isa(K)`` (and ``type(n) == K``) narrows ``node`` to
``K`` on the right of ``&&`` and in the first branch of ``?:``. It narrows
**a variable**: the rule's ``node``, a ``let``, a helper's parameter, a
comprehension's variable. It does not narrow a function's result, which is
recomputed each time it appears:

.. code-block:: yaml

   # Refused: parent(node) is a node, and the second parent(node) is not narrowed.
   where: parent(node).isa(Conversion) && parent(node).type.name == "void"

   # Accepted: a helper's parameter narrows.
   define:
     void_cast(e): e.isa(Conversion) && e.type.name == "void"
   where: void_cast(parent(node))

   # Accepted: so does a let.
   let:
     p: parent(node)
   where: p.isa(Conversion) && p.type.name == "void"

``enclosing(n, K)`` and ``descendants(n, K)`` give results of class ``K``
already, so their fields can be read directly (``enclosing(node,
Subroutine).name``), once it is known not to be null.

Messages
========

A ``message`` interpolates ``${...}`` expressions: a string, a number, a bool,
a node (shown by its name or path), or a list of any of those, written
``, ``-separated. ``join``, ``substring`` and ``flatten`` (cel-go's strings
and lists extensions) shape text and lists:

.. code-block:: yaml

   message: >-
     package ${node.name} holds ${kinds(node)} content
   # or, with a separator of your own: ${kinds(node).join(" and ")}

Contexts (experimental)
=======================

A rule file can classify the code into **contexts** before its rules run:
labels on a few axes (``unit``: agent, env, test, …; ``provenance``: user,
vip, …), attached to **carriers** (packages, interfaces and modules, and
classes) and inherited by everything inside them. The pack names the labels;
svcheck knows only labels, axes and carriers.

.. code-block:: yaml

   contexts:
     - label: agent
       axis: unit
       carrier: Package
       when: '!carrier.inLibrary && classes(carrier).exists(c, is(c, "uvm_agent"))'

A rule then reads them with ``applies_to: { unit: agent }`` (it runs only
where the innermost carrier with a ``unit`` label has ``agent``), with
``context(node)`` / ``context(node, "unit")``, or by matching the ``Unit``
and ``UnitDep`` kinds.

How a carrier's labels are decided, on each axis:

- **Declared** labels win: a ``// svcheck context: agent(apb)`` comment before
  a package, class, module or interface, the ``svcheck_context`` attribute,
  or a ``contexts`` entry of the configuration (``svcheck --config``), which
  may name an axis no classifier has, such as ``provenance``.
- Else every **content** classifier's label (``strength: strong``, the
  default): what the code is and does.
- Else the **convention** ones (``strength: weak``: names, directories). A
  label only convention gives **never selects**: ``applies_to``, context
  overrides and a unit's ``labels`` ignore it. It is kept in the unit's
  ``conventionLabels``, and a weaker source that disagrees with the one used
  is kept in its ``disagreements``, for a consistency rule.

A carrier has the labels of the carriers inside it too (a package with an
agent class and an env class is ``{agent, env}``). A labelled class joins its
package's unit, unless the package is **mixed** (more than one label on the
axis). Inside a mixed package a class's context is its own: an unlabelled
class there has no context on that axis, so an agent rule does not run on a
sequence or scoreboard beside the agent. A classifier's ``joins:`` gives other
carriers its label and unit: an interface the package fetches from
``config_db``, or one a class of the package is specialised with anywhere
(``specializations(c)``)::

   joins: >-
     classes(carrier).map(k, specializations(k)).flatten()
       .map(t, t.args.filter(a, a.isVirtualInterface)).flatten()
       .map(a, a.ifaceDecl)

``svcheck --contexts`` prints each unit, its labels and the evidence for them.

Examples are tests
==================

Each rule's ``examples:`` are snippets that must give exactly the findings
their ``// expect: ID`` comments mark, and a near-miss with none. An example
is compiled on its own; one that mentions ``uvm_`` is compiled with
``uvm_pkg``, from the directory ``--uvm`` names. ``svcheck --check-rules
--rules FILE --uvm UVM/src`` checks the rule files and runs every example.

Also see :doc:`functions` for every function a rule can call, and
:doc:`../model/index` for every model kind and field.

For coding agents
=================

The wheel carries two agent skills. ``svcheck-rules`` walks an agent
through writing a rule from a plain-language description: this page's
workflow, the model and function references, the UVM helpers and slang's
syntax kinds. ``svcheck`` covers running svcheck (:doc:`../cli`): compiling a
bench, choosing rules and presets, output, waivers, the configuration, CI and
triaging findings. Each is registered as an ``agent.skills`` entry point, so
a tool that discovers skills from installed packages (IVPM) links it into
``.claude/skills/`` and the like; their sources are ``skills/<name>/`` in
svcheck's repository.
