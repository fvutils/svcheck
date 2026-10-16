# SL-1: false "cyclic dependency" on a derived interface parameter

| | |
|---|---|
| **Status** | 2026-10-16: present in slang v11.0 (svcheck's pin) and v12.0 (latest release, 2026-09-29). No upstream issue reports it, and no commit on `master` since v11 touches the code path. **Not reported upstream** (tracked locally, user 2026-10-16); the draft is below. **Patched** in svcheck's slang: `0001-hasSameType-elaborate-bodies-first.patch`. |
| **Found in** | fw-wb-dma, svcheck-plan P1-24: 3 errors, one per Wishbone transactor kind |
| **Repro** | `tests/host/slang/sl1-param-cycle.sv` (12 lines), run by `tests/host/test_slang_patches.py`. Verilator 5.041 and 5.052 accept it |
| **Workaround** | `--disable-instance-caching` (a slang option, so svcheck takes it too). Not needed with the patch: svcheck keeps instance caching on (user, 2026-10-16) |
| **Fix** | `0001-hasSameType-elaborate-bodies-first.patch` (on v11.0). The same change on v12.0: the repro compiles, fw-wb-dma loses its 3 cycle errors, and slang's unit tests all pass (2,500 test cases, 22,993 assertions) |

## What triggers it

Stack at the diagnostic (debug build of v11.0; frames numbered from the error):

| Frames | What slang is doing |
|---|---|
| #47–#38 | binding `f(u_m1.u_if)`: is the argument assignment-compatible with `virtual x_if #(32)`? |
| #36–#34 | `Type::isMatching` → `InstanceBodySymbol::hasSameType` compares the two interface bodies' parameter values |
| #33 | `ParameterSymbol::getValue` on `W`, not yet evaluated: marks it *evaluating*, binds `A + 1` |
| #25–#20 | looking up `A` is the first lookup into this body, so the scope elaborates lazily (`getNameMap` → `ensureElaborated` → `Scope::elaborate`) |
| #19–#12 | `InstanceBodySymbol::finishElaboration` forces the port types, `d`'s `[W-1:0]` among them |
| #0 | `W.getValue()` again, still *evaluating*: `ConstEvalParamCycle` |

The cycle is not in the design. It is the order of two lazy steps: reading a parameter starts a
lookup, the lookup elaborates the body, and elaborating the body reads the same parameter.

The body is unelaborated at that point only because of **instance caching**. The two instances of
`x_xtor` have the same parameters, so slang elaborates the first body and skips the second as a
duplicate. The second instance's interface body is still untouched when the class's code reaches
it. That explains each condition:

| Condition | Why it is needed |
|---|---|
| two identical instances | the second is a cache duplicate, which elaboration skips |
| the *second* instance referenced | the first one's body was elaborated in the normal pass, so `W` already has its value |
| `W` derived (`A + 1`) | only an initializer that looks a name up triggers the lazy elaboration; `W = 33` does not |
| `W` used in a port | `finishElaboration` forces port types only |
| a virtual-interface type comparison | `hasSameType` is what reads `W` from outside, first; an `initial` block works as well as a class method |

`--disable-instance-caching` removes it, on the repro and on fw-wb-dma.

## The fix tried

`hasSameType` elaborates both bodies before it compares their parameters
(`0001-hasSameType-elaborate-bodies-first.patch`). The elaboration then happens outside `W`'s evaluation. That is one
call site. A more general fix would make the lazy elaboration that a lookup starts not force
port types while a parameter of the same body is being evaluated; upstream will know which they
prefer.

## Draft issue (not filed)

> **False "cyclic dependency" when comparing a virtual interface type against a cached duplicate instance**
>
> slang v12.0 (also v11.0) reports `cyclic dependency found when determining value of 'W'` on
> this code, which Verilator accepts. `--disable-instance-caching` makes it go away.
>
> ```systemverilog
> interface x_if #(parameter int A = 32, parameter int W = A + 1) (output reg [W-1:0] d);
> endinterface
> module x_xtor; x_if u_if (); endmodule
> module top;
>   x_xtor u_m0 ();
>   x_xtor u_m1 ();
>   class c;
>     function void f(virtual x_if #(32) v);
>       f(u_m1.u_if);
>     endfunction
>   endclass
> endmodule
> ```
>
> It needs all of: two identical instances (the second is a cached duplicate whose body is not
> elaborated), a reference to the second one's interface through a virtual interface type, and a
> parameter derived from another parameter that is used in a port.
>
> The stack: `hasSameType` → `ParameterSymbol::getValue(W)` (marks it evaluating) → binding `A` →
> `Lookup` → `Scope::ensureElaborated` → `InstanceBodySymbol::finishElaboration` → port type
> `[W-1:0]` → `getValue(W)` again → `ConstEvalParamCycle`.
>
> Elaborating both bodies at the top of `InstanceBodySymbol::hasSameType` (`ensureElaborated()`
> on `this` and `other`) fixes it here and in the code it came from.
