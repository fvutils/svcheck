Node Reference
==============

The classes, fields and enums of svcheck's curated model of a SystemVerilog
design: what a rule's expressions read when it matches a model kind
(``match: { kind: Class }``). Every page is generated from ``src/ast/*.yaml``,
the same files the model's C++ is generated from, when the documentation is
built. How to write rules against it is in :doc:`../rules/authoring`; the
functions they call are in :doc:`../rules/functions`.

The model is lowered from slang's elaborated design. User code is lowered in
full; library code (``uvm_pkg``, a VIP given with ``--library-dir``) as
declarations only, with no subroutine bodies. What the model does not curate
is an ``Opaque`` node (``OpaqueDecl``, ``OpaqueStmt``, ``OpaqueExpr``) that
names slang's kind and keeps the children, so nothing under it is lost to a
rule that walks.

Reading an entry
----------------

Each class entry gives, in order:

* **Inherits** -- the chain of base classes, and **Subclasses**.
* What the class represents, from its ``doc:`` in the YAML.
* A diagram of its neighbourhood: its base, its subclasses, what it holds,
  and the classes whose fields are declared as it. Click a class to go to it.
* **Fields** -- each field's type, how it relates the two classes (owns,
  owns a list of, refers to), whether the classify stage rather than the
  lowering sets it, and what it means. Fields of base classes are listed
  under **Inherited fields**.
* **Appears in** -- every field that can hold the class.
* **In each API** -- how each field is read by a rule (``node.lhs``, with
  the type the rule checker gives it) and in C++ (``svcheck::sem``).

Diagram conventions
-------------------

.. astb:legend:: sv

Map of the model
----------------

One box per group of classes (one YAML file, one page); an arrow where
classes in one group own classes in another, labelled with the number of
fields that do. Click a box, or a name in the list, to go to its page.

.. astb:model-map:: sv

.. toctree::
   :hidden:

   core
   decl
   type
   stmt
   expr
   unit

Class hierarchy
---------------

Every class, by inheritance, from ``SemNode``.

.. astb:inheritance-tree:: SemNode
   :model: sv
