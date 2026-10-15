"""svcheck: a SystemVerilog checker whose rules are data.

P0: the package ships the native ``svcheck`` executable and a console script
that runs it. Bindings arrive with the engine (svcheck-plan P1, P2).
"""
from .__version__ import version as __version__
from .native import native_path

__all__ = ["__version__", "native_path"]
