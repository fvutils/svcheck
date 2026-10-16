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
#include <vector>
#include "Schema.h"
#include "SlangSchema.h"
#include "SvLib.h"
#include "reflect/Reflect.h"
#include "svcheck/sem/IClass.h"
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
    auto *cls = dynamic_cast<sem::IClass *>(sem::reflect::asSemNode(n));
    if (!cls) {
        return Value::boolean(false);
    }
    c.steps = 1 + cls->getSupers().size();
    if (classIs(cls, name)) {
        return Value::boolean(true);
    }
    for (const sem::IClass *b : cls->getSupers()) {
        if (classIs(b, name)) {
            return Value::boolean(true);
        }
    }
    return Value::boolean(false);
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
        "Whether a class is the one named or derives from it: "
        "`derives_from(c, \"uvm_object\")`. The name is plain, or qualified by its "
        "package (`uvm_pkg::uvm_object`). False for a node that is not a class, and for "
        "null. One step per class in the chain.")
        .nullable = true;
}

}
}
