/*
 * SvLib.cpp
 *
 * Copyright 2026 Matthew Ballance and Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * The kind test and navigation functions are pssparser's (lib/PssLib.cpp at
 * 57c6b14), unchanged but for the library name: they read only the schema
 * and the run's parent links, so they serve every layer.
 */
#include <string>
#include <unordered_set>
#include <vector>
#include "Schema.h"
#include "SlangSchema.h"
#include "SvContext.h"
#include "SvLib.h"
#include "Syntax.h"
#include "reflect/Reflect.h"
#include "svcheck/sem/ICall.h"
#include "svcheck/sem/IClass.h"
#include "svcheck/sem/IDefinition.h"
#include "svcheck/sem/IMemberAccess.h"
#include "svcheck/sem/INamedValue.h"
#include "svcheck/sem/INewClass.h"
#include "svcheck/sem/IPackage.h"
#include "svcheck/sem/ISubroutine.h"
#include "svcheck/sem/ITypeRef.h"
#include "svcheck/sem/ITypedef.h"
#include "svcheck/sem/IVariable.h"
#include "cel/CelNode.h"
#include "cel/CelValue.h"
#include "run/Context.h"
#include "run/Walker.h"

namespace svcheck {
namespace host {

namespace {

namespace cel = lint::cel;
namespace rf = svcheck::ast::reflect;
using cel::CallArgs;
using cel::Value;
using lint::run::Context;
using lint::run::Walker;

rf::Node toNode(const Value &v) {
    rf::Node n;
    n.kind = v.node.kind;
    n.ptr = v.node.ptr;
    return n;
}

Value nodeValue(const rf::Node &n) {
    cel::NodeRef r;
    r.kind = n.kind;
    r.ptr = n.ptr;
    return Value::mkNode(r);
}

Value nodeList(const std::vector<rf::Node> &nodes) {
    cel::ValueList l;
    l.reserve(nodes.size());
    for (const rf::Node &n : nodes) {
        l.push_back(nodeValue(n));
    }
    return Value::mkList(std::move(l));
}

// The run's context, or null when the call has none (CelEval on its own).
const Context *context(const CallArgs &c) {
    return dynamic_cast<const Context *>(c.host);
}

Value noContext(const char *fn) {
    return Value::error(std::string(fn) + "() needs the parent links of a lint run");
}

// The class a type value names; false for a type that is not a node class
// (the checker refuses those where it can see them).
bool classOf(const Value &t, uint16_t &cls) {
    return t.kind == Value::Kind::Type && cel::lookupClass(t.s, cls);
}

//* Kind tests

Value isaImpl(CallArgs &c) {
    uint16_t cls;
    if (!classOf(c[0], cls)) {
        return Value::error("isa(): '" + c[0].s + "' is not a node class");
    }
    return Value::boolean(cel::isSubclass(c.target->node.kind, cls));
}

//* Navigation

Value parentImpl(CallArgs &c) {
    const Context *ctx = context(c);
    if (!ctx) {
        return noContext("parent");
    }
    rf::Node p = ctx->parentOf(toNode(c[0]));
    return p ? nodeValue(p) : Value::null();
}

Value ancestorsImpl(CallArgs &c) {
    const Context *ctx = context(c);
    if (!ctx) {
        return noContext("ancestors");
    }
    std::vector<rf::Node> out;
    for (rf::Node p=ctx->parentOf(toNode(c[0])); p; p=ctx->parentOf(p)) {
        out.push_back(p);
    }
    c.steps = out.size();
    return nodeList(out);
}

Value enclosingImpl(CallArgs &c) {
    const Context *ctx = context(c);
    if (!ctx) {
        return noContext("enclosing");
    }
    uint16_t cls;
    if (!classOf(c[1], cls)) {
        return Value::error("enclosing(): '" + c[1].s + "' is not a node class");
    }
    for (rf::Node p=ctx->parentOf(toNode(c[0])); p; p=ctx->parentOf(p)) {
        c.steps++;
        if (cel::isSubclass(p.kind, cls)) {
            return nodeValue(p);
        }
    }
    return Value::null();
}

Value childrenImpl(CallArgs &c) {
    std::vector<rf::Node> out;
    Walker::children(toNode(c[0]), out);
    c.steps = out.size();
    return nodeList(out);
}

Value descendantsImpl(CallArgs &c) {
    // Pre-order, as the walk visits them: a node, then its children's trees.
    std::vector<rf::Node> out, work, kids;
    Walker::children(toNode(c[0]), kids);
    work.assign(kids.rbegin(), kids.rend());
    while (!work.empty()) {
        rf::Node n = work.back();
        work.pop_back();
        out.push_back(n);
        kids.clear();
        Walker::children(n, kids);
        work.insert(work.end(), kids.rbegin(), kids.rend());
    }
    c.steps = out.size();
    return nodeList(out);
}

Value descendantsOfImpl(CallArgs &c) {
    // descendants(), keeping the nodes of one class: the walk is the same,
    // and costs one step per node visited, not the comprehension's dozen.
    uint16_t cls;
    if (!classOf(c[1], cls)) {
        return Value::error("descendants(): '" + c[1].s + "' is not a node class");
    }
    std::vector<rf::Node> out, work, kids;
    Walker::children(toNode(c[0]), kids);
    work.assign(kids.rbegin(), kids.rend());
    while (!work.empty()) {
        rf::Node n = work.back();
        work.pop_back();
        c.steps++;
        if (cel::isSubclass(n.kind, cls)) {
            out.push_back(n);
        }
        kids.clear();
        Walker::children(n, kids);
        work.insert(work.end(), kids.rbegin(), kids.rend());
    }
    return nodeList(out);
}

//* Classes

// Whether a class is `name` or derives from it; `name` is plain
// (`uvm_object`) or qualified (`uvm_pkg::uvm_object`).
bool classIs(const sem::IClass *c, const std::string &name) {
    bool q = name.find("::") != std::string::npos;
    return (q ? c->getQname() : c->getName()) == name;
}

Value derivesFromImpl(CallArgs &c) {
    rf::Node n = toNode(c[0]);
    const std::string &name = c[1].s;
    if (installedLayer() == SemanticLayer::Slang) {
        c.steps = 1;
        return Value::boolean(slangDerivesFrom(n, name));
    }
    if (installedLayer() != SemanticLayer::Model) {
        return Value::boolean(false);
    }
    // A class, or a type: a class handle's class.
    sem::ISemNode *s = sem::reflect::asSemNode(n);
    if (auto *t = dynamic_cast<sem::ITypeRef *>(s)) {
        s = t->getClassDecl();
    }
    auto *cls = dynamic_cast<sem::IClass *>(s);
    if (!cls) {
        return Value::boolean(false);
    }
    c.steps = 1 + cls->getSupers().size() + cls->getInterfaces().size();
    if (classIs(cls, name)) {
        return Value::boolean(true);
    }
    for (const sem::IClass *b : cls->getSupers()) {
        if (classIs(b, name)) {
            return Value::boolean(true);
        }
    }
    for (const sem::IClass *i : cls->getInterfaces()) {
        if (classIs(i, name)) {
            return Value::boolean(true);
        }
    }
    return Value::boolean(false);
}

//* The curated model (svcheck-plan P1.4). These read the model's nodes, and
//* are registered only when it is the layer installed.

// The model node a value holds; null for null, or for another layer's node.
sem::ISemNode *semOf(const Value &v) {
    return (v.kind == Value::Kind::Node) ? sem::reflect::asSemNode(toNode(v)) : nullptr;
}

Value semValue(sem::ISemNode *n) {
    return n ? nodeValue(sem::reflect::classify(n)) : Value::null();
}

Value semList(const std::vector<sem::ISemNode *> &nodes) {
    cel::ValueList l;
    l.reserve(nodes.size());
    for (sem::ISemNode *n : nodes) {
        l.push_back(semValue(n));
    }
    return Value::mkList(std::move(l));
}

sem::IDecl *findMember(sem::IClass *c, const std::string &name) {
    for (const sem::IDeclUP &m : c->getMembers()) {
        if (m->getName() == name) {
            return m.get();
        }
    }
    return nullptr;
}

Value memberImpl(CallArgs &c) {
    auto *cls = dynamic_cast<sem::IClass *>(semOf(c[0]));
    if (!cls) {
        return Value::null();
    }
    c.steps = 1;
    if (sem::IDecl *d = findMember(cls, c[1].s)) {
        return semValue(d);
    }
    for (sem::IClass *b : cls->getSupers()) {
        c.steps++;
        if (sem::IDecl *d = findMember(b, c[1].s)) {
            return semValue(d);
        }
    }
    return Value::null();
}

Value membersImpl(CallArgs &c) {
    auto *cls = dynamic_cast<sem::IClass *>(semOf(c[0]));
    if (!cls) {
        return Value::mkList({});
    }
    std::vector<sem::ISemNode *> out;
    std::unordered_set<std::string> seen;
    auto add = [&](sem::IClass *k) {
        for (const sem::IDeclUP &m : k->getMembers()) {
            if (seen.insert(m->getName()).second) {
                out.push_back(m.get());
            }
        }
    };
    add(cls);
    for (sem::IClass *b : cls->getSupers()) {
        add(b);
    }
    c.steps = out.size();
    return semList(out);
}

// The nearest class, package or definition above `n` in the run's tree.
sem::IDecl *ownerOf(const Context &ctx, const rf::Node &n, uint64_t &steps) {
    for (rf::Node p=ctx.parentOf(n); p; p=ctx.parentOf(p)) {
        steps++;
        sem::ISemNode *s = sem::reflect::asSemNode(p);
        if (dynamic_cast<sem::IClass *>(s) || dynamic_cast<sem::IPackage *>(s)
                || dynamic_cast<sem::IDefinition *>(s)) {
            return dynamic_cast<sem::IDecl *>(s);
        }
    }
    return nullptr;
}

Value ownerImpl(CallArgs &c) {
    const Context *ctx = context(c);
    if (!ctx) {
        return noContext("owner");
    }
    if (!semOf(c[0])) {
        return Value::null();
    }
    return semValue(ownerOf(*ctx, toNode(c[0]), c.steps));
}

Value inLibraryImpl(CallArgs &c) {
    sem::ISemNode *n = semOf(c[0]);
    if (auto *d = dynamic_cast<sem::IDecl *>(n)) {
        return Value::boolean(d->getInLibrary());
    }
    const Context *ctx = context(c);
    if (!n || !ctx) {
        return Value::boolean(false);
    }
    for (rf::Node p=ctx->parentOf(toNode(c[0])); p; p=ctx->parentOf(p)) {
        c.steps++;
        if (auto *d = dynamic_cast<sem::IDecl *>(sem::reflect::asSemNode(p))) {
            return Value::boolean(d->getInLibrary());
        }
    }
    return Value::boolean(false);
}

Value resolvesToImpl(CallArgs &c) {
    sem::ISemNode *n = semOf(c[0]);
    if (auto *v = dynamic_cast<sem::INamedValue *>(n)) {
        return semValue(v->getDecl());
    } else if (auto *m = dynamic_cast<sem::IMemberAccess *>(n)) {
        return semValue(m->getDecl());
    } else if (auto *k = dynamic_cast<sem::ICall *>(n)) {
        return semValue(k->getSubroutine());
    } else if (auto *nc = dynamic_cast<sem::INewClass *>(n)) {
        return semValue(nc->getType() ? nc->getType()->getClassDecl() : nullptr);
    } else if (auto *t = dynamic_cast<sem::ITypeRef *>(n)) {
        return t->getClassDecl() ? semValue(t->getClassDecl()) : semValue(t->getIfaceDecl());
    }
    return Value::null();
}

Value typeOfImpl(CallArgs &c) {
    sem::ISemNode *n = semOf(c[0]);
    if (auto *e = dynamic_cast<sem::IExpr *>(n)) {
        return semValue(e->getType());
    } else if (auto *v = dynamic_cast<sem::IVariable *>(n)) {
        return semValue(v->getType());
    } else if (auto *t = dynamic_cast<sem::ITypedef *>(n)) {
        return semValue(t->getType());
    } else if (auto *s = dynamic_cast<sem::ISubroutine *>(n)) {
        return semValue(s->getReturnType());
    } else if (auto *t = dynamic_cast<sem::ITypeRef *>(n)) {
        return semValue(t);
    }
    return Value::null();
}

Value constIntImpl(CallArgs &c) {
    auto *e = dynamic_cast<sem::IExpr *>(semOf(c[0]));
    if (e && e->getConstKind() == sem::ConstKind::Int) {
        return Value::some(Value::integer(e->getConstInt()));
    }
    return Value::none();
}

Value constStringImpl(CallArgs &c) {
    auto *e = dynamic_cast<sem::IExpr *>(semOf(c[0]));
    if (e && e->getConstKind() == sem::ConstKind::String) {
        return Value::some(Value::string(e->getConstString()));
    }
    return Value::none();
}

// The nodes of class `name` under `n`, in pre-order (descendants(n, K)).
Value descendantsNamed(CallArgs &c, const char *name) {
    uint16_t cls;
    if (!cel::lookupClass(name, cls)) {
        return Value::error(std::string("the schema has no class '") + name + "'");
    }
    std::vector<rf::Node> out, work, kids;
    if (c[0].kind != Value::Kind::Node) {
        return nodeList(out);
    }
    Walker::children(toNode(c[0]), kids);
    work.assign(kids.rbegin(), kids.rend());
    while (!work.empty()) {
        rf::Node n = work.back();
        work.pop_back();
        c.steps++;
        if (cel::isSubclass(n.kind, cls)) {
            out.push_back(n);
        }
        kids.clear();
        Walker::children(n, kids);
        work.insert(work.end(), kids.rbegin(), kids.rend());
    }
    return nodeList(out);
}

Value classesImpl(CallArgs &c) {
    return descendantsNamed(c, "Class");
}

Value callsInImpl(CallArgs &c) {
    return descendantsNamed(c, "Call");
}

Value contextOf(CallArgs &c, const std::string &axis) {
    // No SvContext: classify did not run, so there are no units.
    auto *ctx = dynamic_cast<const SvContext *>(c.host);
    if (!ctx || !semOf(c[0])) {
        return Value::null();
    }
    return semValue(ctx->contextOf(toNode(c[0]), axis));
}

Value subclassesImpl(CallArgs &c) {
    auto *ctx = dynamic_cast<const SvContext *>(c.host);
    auto *cls = dynamic_cast<sem::IClass *>(semOf(c[0]));
    if (!ctx || !cls) {
        return Value::mkList({});
    }
    const std::vector<sem::IClass *> &subs = ctx->subclassesOf(cls);
    c.steps = 1 + subs.size();
    return semList(std::vector<sem::ISemNode *>(subs.begin(), subs.end()));
}

Value contextImpl(CallArgs &c) {
    return contextOf(c, "");
}

Value contextAxisImpl(CallArgs &c) {
    return contextOf(c, c[1].s);
}

//* Both layers.

Value macroOfImpl(CallArgs &c) {
    if (c[0].kind != Value::Kind::Node) {
        return Value::string("");
    }
    if (sem::ISemNode *n = semOf(c[0])) {
        return Value::string(n->getMacro());
    }
    return Value::string(syntaxMacroOf(toNode(c[0])));
}

}

void registerSvLib(cel::FunctionRegistry &r) {
    using C = cel::Cost;
    r.library("sv");
    r.level(cel::Level::L0, cel::Scope::File);

    r.group("Kind tests");
    r.add("node_isa", "node.isa(type) -> bool", &isaImpl, C::Constant,
        "Whether the node is of the class named, or of a class deriving from it: "
        "`node.isa(ClassDeclaration)`. The argument must be a node class. Unlike "
        "`type(n) == K`, which tests the exact class, `isa` follows the schema's "
        "super classes. Like `type(n) == K`, it narrows: on the right of `&&`, and in the "
        "first result of `?:`, `n` has class `K`.")
        .classArg = 1;

    r.group("Navigation");
    r.add("parent_node", "parent(node) -> node", &parentImpl, C::Constant,
        "The node that owns this one: the scope holding a declaration, the expression "
        "holding an operand. `null` for a root, and for a node outside the trees "
        "the run covers.");
    r.add("ancestors_node", "ancestors(node) -> list(node)", &ancestorsImpl, C::Custom,
        "The node's owners, nearest first, ending with the root; empty for a root. "
        "One step per element.");
    cel::Overload &enclosing = r.add("enclosing_node_type", "enclosing(node, type) -> node",
        &enclosingImpl, C::Custom,
        "The nearest owner of the class named, or of a class deriving from it: "
        "`enclosing(node, Class)`. The result has that class, so its fields can be read "
        "directly (`enclosing(node, Class).name`). `null` when there is none; reading a "
        "field through `null` is an error, so test `enclosing(node, K) != null` first "
        "where there may be none. The node itself is not considered. One step per owner "
        "visited.");
    enclosing.classArg = 1;
    enclosing.resultIsClass = true;
    r.add("children_node", "children(node) -> list(node)", &childrenImpl, C::Custom,
        "The nodes this one owns, in source order: each node field and list element of "
        "the node's class, inherited fields first. References to declarations elsewhere "
        "are not children. One step per element.");
    r.add("descendants_node", "descendants(node) -> list(node)", &descendantsImpl, C::Custom,
        "Every node under this one, in pre-order (a node, then its children's subtrees), "
        "not including the node itself. One step per element.");
    cel::Overload &descendantsOf = r.add("descendants_node_type",
        "descendants(node, type) -> list(node)", &descendantsOfImpl, C::Custom,
        "The nodes under this one of the class named, or of a class deriving from it, in "
        "pre-order: `descendants(node, Call)`. The same as "
        "`descendants(node).filter(d, d.isa(Call))`, but the elements have that class, so "
        "their fields can be read directly (`descendants(node, Call).map(c, c.name)`), "
        "and the filtering costs one step per node visited rather than a comprehension's "
        "per-element cost.");
    descendantsOf.classArg = 1;
    descendantsOf.resultIsClassList = true;
    descendantsOf.nullable = true;

    r.group("Classes");
    r.add("derives_from_node_string", "derives_from(node, string) -> bool", &derivesFromImpl,
        C::Custom,
        "Whether a class is the one named or derives from it, through `extends` or "
        "`implements`: `derives_from(c, \"uvm_object\")`. The name is plain, or qualified "
        "by its package (`uvm_pkg::uvm_object`). A type (TypeRef) is its class, so "
        "`derives_from(type_of(e), ...)` asks of an expression's class. False for a node "
        "that is neither, and for null. One step per class in the chain.")
        .nullable = true;

    r.group("Preprocessor");
    r.add("macro_of_node", "macro_of(node) -> string", &macroOfImpl, C::Constant,
        "The macro whose expansion made the node: `\"uvm_object_utils\"` for the `type_id` "
        "typedef that `` `uvm_object_utils(c) `` declares. When expansions nest, the "
        "outermost: the macro written in the source. A macro argument is where it is "
        "written, so `macro_of` of the message in `` `uvm_info(\"ID\", msg, UVM_LOW) `` is "
        "\"\" when the use is not itself in a macro. `\"\"` for a node written out, and "
        "for null. For a syntax node, the macro its first token comes from.")
        .nullable = true;

    if (installedLayer() != SemanticLayer::Model) {
        return;
    }
    r.add("member_node_string", "member(node, string) -> Decl", &memberImpl, C::Custom,
        "The member of a class with this name, its own or inherited, the nearest first: "
        "`member(node, \"type_id\") == null` for an unregistered UVM class. `null` when there "
        "is none, and for a node that is not a class. One step per class searched.")
        .nullable = true;
    r.add("members_node", "members(node) -> list(Decl)", &membersImpl, C::Custom,
        "A class's members, its own and inherited, in declaration order, its own first: a "
        "member hidden by one of the same name nearer the class is left out. Empty for a "
        "node that is not a class. `node.members` is the class's own. One step per member.")
        .nullable = true;
    r.add("owner_node", "owner(node) -> Decl", &ownerImpl, C::Custom,
        "The class, package or definition the node is in, the nearest: a method's class, a "
        "class's package. `null` for a package or definition, which are in none, and for a "
        "node of another layer. One step per owner visited.")
        .nullable = true;

    r.group("Symbols, types, constants");
    r.add("in_library_node", "in_library(node) -> bool", &inLibraryImpl, C::Custom,
        "Whether the node is library code (uvm_pkg, a configured VIP), lowered as "
        "declarations only: a declaration's `inLibrary`, else that of the nearest "
        "declaration it is in. False for null, and for a node of another layer.")
        .nullable = true;
    r.add("resolves_to_node", "resolves_to(node) -> Decl", &resolvesToImpl, C::Constant,
        "The declaration a reference names: a NamedValue's or MemberAccess's `decl`, a "
        "Call's `subroutine`, the class a NewClass constructs, a TypeRef's class or "
        "interface. `null` for anything else, for a name the model does not lower (a "
        "built-in, a parameter), and for null.")
        .nullable = true;
    r.add("type_of_node", "type_of(node) -> TypeRef", &typeOfImpl, C::Constant,
        "The type of an expression, variable or typedef, or a subroutine's return type: "
        "`type_of(node).isVirtualInterface`. A TypeRef is itself. `null` for another node.")
        .nullable = true;
    r.add("const_int_node", "const_int(node) -> optional(int)", &constIntImpl, C::Constant,
        "The value of a constant integer expression: a literal, a parameter, an enum value, "
        "or an operator over those, as slang evaluates it. `optional.none()` for anything "
        "else: a call, a variable, a value with X or Z bits, one that does not fit 64 bits.")
        .nullable = true;
    r.add("const_string_node", "const_string(node) -> optional(string)", &constStringImpl,
        C::Constant,
        "The value of a constant string expression: a string literal, or a parameter or "
        "operator over constants whose type is `string`. `optional.none()` for anything else.")
        .nullable = true;

    r.group("Carriers");
    r.add("classes_node", "classes(node) -> list(Class)", &classesImpl, C::Custom,
        "The classes declared in a package, definition or class, nested ones too, in "
        "declaration order: what a context classifier looks through "
        "(`classes(carrier).exists(c, is(c, \"uvm_agent\"))`). One step per node visited.")
        .nullable = true;
    r.add("context_node", "context(node) -> Unit", &contextImpl, C::Custom,
        "The context unit of the node (sv-contexts.md; experimental): that of the innermost "
        "carrier, the node itself or one it is in, that the classify stage labelled. "
        "`\"agent\" in context(node).labels`. `null` when no carrier above it has a unit, "
        "and when the run has no `contexts:`. One step per owner visited.")
        .nullable = true;
    r.add("context_node_string", "context(node, string) -> Unit", &contextAxisImpl, C::Custom,
        "`context(node)`, but the innermost carrier with a label on the axis named: "
        "`context(node, \"unit\")`. An agent class in a mixed package answers with the "
        "class's unit, not the package's (Q-CX3).")
        .nullable = true;
    r.add("subclasses_node", "subclasses(node) -> list(Class)", &subclassesImpl, C::Custom,
        "The classes whose base is the class given, in declaration order, user and library "
        "code: `subclasses(c).exists(s, s.carrier != c.carrier)` says another package "
        "derives from `c` (a base library's class, svcheck-plan P1.6b). Empty for a node "
        "that is not a class. One step per class returned.")
        .nullable = true;
    r.add("calls_in_node", "calls_in(node) -> list(Call)", &callsInImpl, C::Custom,
        "The calls in a carrier's code, or in any node's subtree, in pre-order: "
        "`descendants(node, Call)`. Library code has no bodies, so none of its calls. One "
        "step per node visited.")
        .nullable = true;
}

}
}
