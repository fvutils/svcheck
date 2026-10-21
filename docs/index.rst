svcheck
=======

svcheck checks SystemVerilog against declarative rules: ``sv-rules`` files,
YAML with CEL expressions, run over slang's syntax trees and over a curated
semantic model of the elaborated design.

.. code-block:: sh

   pip install svcheck
   svcheck --preset uvm-consensus -f tb.f

The wheels are for Linux x86_64, CPython 3.10 to 3.14. ``contexts:`` and
``applies_to:`` are experimental in this release.

.. toctree::
   :maxdepth: 2

   cli
   python
   rules/authoring
   rules/functions
   model/index
