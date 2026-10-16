/*
 * LintTestHooks.cpp
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
 */
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>
#include "Reflect.h"
#include "LintTestHooks.h"
#include "LintEngine.h"
#include "cel/CelCheck.h"
#include "cel/CelEval.h"
#include "cel/CelFunctions.h"
#include "cel/CelNode.h"
#include "cel/CelParser.h"
#include "regex/Regex.h"
#include "rules/RuleLoader.h"
#include "run/Classify.h"
#include "run/Examples.h"
#include "run/Runner.h"
#include "run/Walker.h"
#include "util/JsonWriter.h"
#include "yaml/SourceMap.h"
#include "yaml/YamlDoc.h"

namespace svcheck {
namespace lint {

namespace {

void writeMark(JsonWriter &w, const yaml::Mark &m) {
    w.kv("line", m.line).kv("col", m.col).kv("offset", m.offset);
}

void writeYaml(JsonWriter &w, const yaml::Node *n) {
    using K = yaml::Node::Kind;
    w.beginObject();
    w.kv("kind", yaml::kindName(n->kind));
    w.kv("style", yaml::styleName(n->style));
    writeMark(w, n->mark);
    switch (n->kind) {
        case K::Null: break;
        case K::Bool: w.kv("value", n->b); break;
        case K::Int: w.kv("value", n->i); break;
        case K::Float: w.kv("value", n->d); break;
        case K::String: w.kv("value", n->s); break;
        case K::Map:
            w.key("members").beginArray();
            for (const yaml::Node::Member &m : n->members) {
                w.beginObject();
                w.kv("key", m.key);
                w.key("key_mark").beginObject();
                writeMark(w, m.keyMark);
                w.endObject();
                w.key("value");
                writeYaml(w, m.value.get());
                w.endObject();
            }
            w.endArray();
            break;
        case K::Seq:
            w.key("items").beginArray();
            for (const yaml::NodeUP &c : n->items) {
                writeYaml(w, c.get());
            }
            w.endArray();
            break;
    }
    w.endObject();
}

void writeConst(JsonWriter &w, const cel::Const &c) {
    using K = cel::Const::Kind;
    switch (c.kind) {
        case K::Null: w.kv("type", "null").key("value").valueNull(); break;
        case K::Bool: w.kv("type", "bool").kv("value", c.b); break;
        case K::Int: w.kv("type", "int").kv("value", c.i); break;
        case K::Uint: w.kv("type", "uint").kv("value", c.u); break;
        case K::Double: w.kv("type", "double").kv("value", c.d); break;
        case K::String: w.kv("type", "string").kv("value", c.s); break;
        case K::Bytes: {
            // Hex, so the bytes survive both bindings' UTF-8 string conversion.
            static const char *hex = "0123456789abcdef";
            std::string h;
            for (unsigned char b : c.s) {
                h += hex[b >> 4];
                h += hex[b & 0xF];
            }
            w.kv("type", "bytes").kv("value", h);
        } break;
    }
}

void writeExpr(JsonWriter &w, const cel::Expr *e) {
    using K = cel::ExprKind;
    if (!e) {
        w.valueNull();
        return;
    }
    w.beginObject();
    w.kv("id", e->id).kv("begin", e->begin).kv("end", e->end);
    switch (e->kind) {
        case K::Const:
            w.kv("k", "const");
            writeConst(w, e->cval);
            break;
        case K::Ident:
            w.kv("k", "ident").kv("name", e->name);
            break;
        case K::Select:
            w.kv("k", "select").kv("field", e->name).kv("test_only", e->testOnly);
            w.key("operand");
            writeExpr(w, e->operand.get());
            break;
        case K::Call:
            w.kv("k", "call").kv("fn", e->name);
            if (e->operand) {
                w.key("target");
                writeExpr(w, e->operand.get());
            }
            w.key("args").beginArray();
            for (const cel::ExprUP &a : e->args) {
                writeExpr(w, a.get());
            }
            w.endArray();
            break;
        case K::List:
            w.kv("k", "list");
            w.key("elems").beginArray();
            for (const cel::ExprUP &a : e->args) {
                writeExpr(w, a.get());
            }
            w.endArray();
            w.key("optional").beginArray();
            for (uint32_t i : e->optIndices) {
                w.value(i);
            }
            w.endArray();
            break;
        case K::Map:
        case K::Struct:
            w.kv("k", (e->kind == K::Map) ? "map" : "struct");
            if (e->kind == K::Struct) {
                w.kv("name", e->name);
            }
            w.key("entries").beginArray();
            for (const cel::Entry &ent : e->entries) {
                w.beginObject();
                w.kv("optional", ent.optional);
                if (ent.key) {
                    w.key("key");
                    writeExpr(w, ent.key.get());
                } else {
                    w.kv("field", ent.field);
                }
                w.key("value");
                writeExpr(w, ent.value.get());
                w.endObject();
            }
            w.endArray();
            break;
        case K::Comprehension:
            w.kv("k", "comprehension").kv("iter_var", e->iterVar).kv("accu_var", e->accuVar);
            w.key("iter_range"); writeExpr(w, e->iterRange.get());
            w.key("accu_init"); writeExpr(w, e->accuInit.get());
            w.key("loop_cond"); writeExpr(w, e->loopCond.get());
            w.key("loop_step"); writeExpr(w, e->loopStep.get());
            w.key("result"); writeExpr(w, e->result.get());
            break;
    }
    w.endObject();
}

void writeValue(JsonWriter &w, const cel::Value &v);

// A node is shown by its class and, where the class has one, its location:
// enough to tell nodes apart in a test without exposing pointers.
void writeNode(JsonWriter &w, const cel::Value &v) {
    w.beginObject();
    w.kv("class", cel::nodeClassName(v.node.kind));
    cel::Value loc;
    uint64_t elems;
    if (cel::readField(v, "location", loc, elems) && loc.kind == cel::Value::Kind::Struct) {
        cel::Value line, col;
        cel::readField(loc, "lineno", line, elems);
        cel::readField(loc, "linepos", col, elems);
        w.key("loc").beginArray().value(line.i).value(col.i).endArray();
    }
    w.endObject();
}

void writeValue(JsonWriter &w, const cel::Value &v) {
    using K = cel::Value::Kind;
    w.beginObject();
    w.kv("type", cel::kindName(v.kind));
    w.key("value");
    switch (v.kind) {
        case K::Null: w.valueNull(); break;
        case K::Bool: w.value(v.b); break;
        case K::Int: w.value(v.i); break;
        case K::Double: w.value(v.d); break;
        case K::String: w.value(v.s); break;
        case K::Error: w.value(v.s); break;
        case K::Type: w.value(v.s); break;
        case K::List:
            w.beginArray();
            for (const cel::Value &e : *v.list) {
                writeValue(w, e);
            }
            w.endArray();
            break;
        case K::Map:
            w.beginArray();
            for (const auto &kv : *v.map) {
                w.beginArray();
                writeValue(w, kv.first);
                writeValue(w, kv.second);
                w.endArray();
            }
            w.endArray();
            break;
        case K::Optional:
            if (v.opt) {
                writeValue(w, *v.opt);
            } else {
                w.valueNull();
            }
            break;
        case K::Node:
            writeNode(w, v);
            break;
        case K::Struct: {
            if (const cel::RecordInfo *ri = cel::recordInfo(static_cast<uint16_t>(v.i))) {
                w.beginObject();
                w.kv("struct", ri->name);
                for (size_t i=0; i<ri->members.size() && v.list && i<v.list->size(); i++) {
                    w.key(ri->members[i].name);
                    writeValue(w, (*v.list)[i]);
                }
                w.endObject();
                break;
            }
            const svcheck::ast::reflect::StructInfo &si =
                svcheck::ast::reflect::structs[static_cast<uint16_t>(v.i)];
            w.beginObject();
            w.kv("struct", si.name);
            for (uint16_t i=0; i<si.numFields; i++) {
                cel::Value mv;
                uint64_t elems;
                cel::readField(v, si.fields[i].name, mv, elems);
                w.kv(si.fields[i].name, mv.i);
            }
            w.endObject();
        } break;
    }
    w.endObject();
}

// Bindings for the eval hooks: a JSON object, name -> value.
cel::Value fromYaml(const yaml::Node *n) {
    using K = yaml::Node::Kind;
    switch (n->kind) {
        case K::Null: return cel::Value::null();
        case K::Bool: return cel::Value::boolean(n->b);
        case K::Int: return cel::Value::integer(n->i);
        case K::Float: return cel::Value::dbl(n->d);
        case K::String: return cel::Value::string(n->s);
        case K::Seq: {
            cel::ValueList l;
            for (const yaml::NodeUP &c : n->items) {
                l.push_back(fromYaml(c.get()));
            }
            return cel::Value::mkList(std::move(l));
        }
        case K::Map: {
            cel::ValueMap m;
            for (const yaml::Node::Member &mb : n->members) {
                m.emplace_back(cel::Value::string(mb.key), fromYaml(mb.value.get()));
            }
            return cel::Value::mkMap(std::move(m));
        }
    }
    return cel::Value::null();
}

std::vector<std::pair<std::string, cel::Value>> parseBindings(const std::string &json) {
    std::vector<std::pair<std::string, cel::Value>> vars;
    if (json.empty()) {
        return vars;
    }
    yaml::NodeUP root = yaml::parse("bindings.json", json);
    for (const yaml::Node::Member &m : root->members) {
        vars.emplace_back(m.key, fromYaml(m.value.get()));
    }
    return vars;
}

void writeYamlError(JsonWriter &w, const yaml::YamlError &e) {
    w.kv("ok", false);
    w.key("error").beginObject();
    w.kv("message", e.what());
    writeMark(w, e.mark());
    if (e.hasRelated) {
        w.key("related").beginObject();
        writeMark(w, e.related);
        w.endObject();
    }
    w.endObject();
}

// Pre-order: string scalars, in document order (keys are not scalars here).
void collectStrings(const yaml::Node *n, std::vector<const yaml::Node *> &out) {
    if (n->kind == yaml::Node::Kind::String) {
        out.push_back(n);
    }
    for (const yaml::Node::Member &m : n->members) {
        collectStrings(m.value.get(), out);
    }
    for (const yaml::NodeUP &c : n->items) {
        collectStrings(c.get(), out);
    }
}

void collectKey(const yaml::Node *n, const std::string &key, std::vector<const yaml::Node *> &out) {
    for (const yaml::Node::Member &m : n->members) {
        if (m.key == key && m.value->kind == yaml::Node::Kind::String) {
            out.push_back(m.value.get());
        }
        collectKey(m.value.get(), key, out);
    }
    for (const yaml::NodeUP &c : n->items) {
        collectKey(c.get(), key, out);
    }
}

namespace rf = svcheck::ast::reflect;

const char *fieldTypeName(rf::FieldType t) {
    static const char *names[] = {
        "bool", "i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64", "f32", "f64", "string",
        "enum", "flags", "struct", "node", "list", "map"};
    return names[static_cast<int>(t)];
}

std::string syntaxError(const cel::SyntaxError &e) {
    JsonWriter w;
    w.beginObject().kv("ok", false);
    w.key("error").beginObject().kv("offset", e.offset()).kv("message", e.what()).endObject();
    w.endObject();
    return w.str();
}

}

LintTestHooks::LintTestHooks() { }

LintTestHooks::~LintTestHooks() { }

std::string LintTestHooks::yamlLoad(const std::string &name, const std::string &text) {
    JsonWriter w;
    w.beginObject();
    try {
        yaml::NodeUP root = yaml::parse(name, text);
        w.kv("ok", true);
        w.key("root");
        writeYaml(w, root.get());
    } catch (const yaml::YamlError &e) {
        writeYamlError(w, e);
    }
    w.endObject();
    return w.str();
}

std::string LintTestHooks::yamlSourceMaps(const std::string &name, const std::string &text) {
    JsonWriter w;
    w.beginObject();
    try {
        yaml::NodeUP root = yaml::parse(name, text);
        std::vector<const yaml::Node *> strs;
        collectStrings(root.get(), strs);
        w.kv("ok", true);
        w.key("scalars").beginArray();
        for (const yaml::Node *n : strs) {
            w.beginObject();
            writeMark(w, n->mark);
            w.kv("style", yaml::styleName(n->style)).kv("value", n->s).kv("exact", n->srcExact);
            w.key("map").beginArray();
            for (uint32_t o : n->srcMap) {
                w.value(o);
            }
            w.endArray();
            w.endObject();
        }
        w.endArray();
    } catch (const yaml::YamlError &e) {
        writeYamlError(w, e);
    }
    w.endObject();
    return w.str();
}

std::string LintTestHooks::yamlCelParse(
        const std::string &name, const std::string &text, const std::string &key) {
    JsonWriter w;
    w.beginObject();
    try {
        yaml::NodeUP root = yaml::parse(name, text);
        std::vector<const yaml::Node *> vals;
        collectKey(root.get(), key, vals);
        yaml::LineIndex lines(text);
        w.kv("ok", true);
        w.key("results").beginArray();
        for (const yaml::Node *n : vals) {
            w.beginObject();
            try {
                cel::CelParser p(n->s);
                p.parse();
                w.kv("ok", true);
            } catch (const cel::SyntaxError &e) {
                yaml::Mark m = lines.at(yaml::sourceOffset(*n, e.offset()));
                w.kv("ok", false).kv("message", e.what()).kv("offset", e.offset());
                w.kv("line", m.line).kv("col", m.col).kv("exact", n->srcExact);
            }
            w.endObject();
        }
        w.endArray();
    } catch (const yaml::YamlError &e) {
        writeYamlError(w, e);
    }
    w.endObject();
    return w.str();
}

std::string LintTestHooks::celParse(const std::string &text) {
    try {
        cel::CelParser p(text);
        cel::ExprUP e = p.parse();
        JsonWriter w;
        w.beginObject().kv("ok", true);
        w.key("ast");
        writeExpr(w, e.get());
        w.key("profile_errors").beginArray();
        for (const cel::ProfileError &pe : cel::checkProfile(e.get())) {
            w.beginObject().kv("offset", pe.offset).kv("message", pe.message).endObject();
        }
        w.endArray();
        w.endObject();
        return w.str();
    } catch (const cel::SyntaxError &e) {
        return syntaxError(e);
    }
}

static std::string evalWith(const std::string &text,
        const std::vector<std::pair<std::string, cel::Value>> &vars, uint64_t max_steps,
        const cel::Host *host=nullptr) {
    try {
        cel::CelParser p(text);
        cel::ExprUP e = p.parse();
        cel::EvalOptions opts;
        if (max_steps) {
            opts.maxSteps = max_steps;
        }
        opts.host = host;
        cel::CelEval ev(opts);
        std::vector<const cel::Overload *> trace;
        ev.setTrace(&trace);
        cel::Value v = ev.eval(e.get(), vars);
        JsonWriter w;
        w.beginObject().kv("ok", true);
        w.key("result");
        writeValue(w, v);
        w.kv("steps", ev.steps()).kv("budget_exceeded", ev.budgetExceeded());
        w.key("calls").beginArray();
        for (const cel::Overload *o : trace) {
            w.value(o->id);
        }
        w.endArray();
        w.endObject();
        return w.str();
    } catch (const cel::SyntaxError &e) {
        return syntaxError(e);
    } catch (const yaml::YamlError &e) {
        JsonWriter w;
        w.beginObject();
        writeYamlError(w, e);
        w.endObject();
        return w.str();
    }
}

std::string LintTestHooks::celEval(
        const std::string &text, const std::string &bindings, uint64_t max_steps) {
    return evalWith(text, parseBindings(bindings), max_steps);
}

std::string LintTestHooks::celFunctions() {
    JsonWriter w;
    w.beginObject();
    w.key("functions").beginArray();
    for (const auto &o : cel::FunctionRegistry::get().all()) {
        w.beginObject();
        w.kv("id", o->id).kv("function", o->function);
        w.kv("kind", o->kind == cel::FnKind::Macro ? "macro"
            : o->kind == cel::FnKind::Operator ? "operator" : "function");
        w.kv("receiver", o->receiver);
        w.key("params").beginArray();
        for (const cel::Type &t : o->params) {
            w.value(t.str());
        }
        w.endArray();
        w.kv("result", o->kind == cel::FnKind::Macro ? std::string() : o->result.str());
        w.kv("display", o->display).kv("group", o->group).kv("library", o->library);
        w.kv("level", cel::levelName(o->level)).kv("scope", cel::scopeName(o->scope));
        w.kv("cost", cel::costName(o->cost)).kv("doc", o->doc);
        w.kv("in_profile", o->inProfile);
        // Implemented by the evaluator itself (non-strict or lazy), so it
        // never appears in an eval's "calls".
        w.kv("evaluator", o->kind != cel::FnKind::Macro && !o->impl);
        w.endObject();
    }
    w.endArray();
    w.key("records").beginArray();
    for (const cel::RecordInfo &ri : cel::records()) {
        w.beginObject();
        w.kv("name", ri.name).kv("doc", ri.doc);
        w.key("members").beginArray();
        for (const cel::RecordMember &m : ri.members) {
            w.beginObject().kv("name", m.name).kv("type", m.type.str()).kv("doc", m.doc).endObject();
        }
        w.endArray();
        w.endObject();
    }
    w.endArray();
    w.endObject();
    return w.str();
}

// A small rule-shaped document for the checker (C3.3), located through the
// YAML source maps (C2.2) as the rule loader (C4.1) will locate its errors.
static std::string checkDoc(const std::string &name, const std::string &text,
        const cel::Value *node, const cel::Host *host=nullptr) {
    JsonWriter w;
    w.beginObject();
    yaml::NodeUP root;
    try {
        root = yaml::parse(name, text);
    } catch (const yaml::YamlError &e) {
        writeYamlError(w, e);
        w.endObject();
        return w.str();
    }
    auto member = [&root](const char *key) -> const yaml::Node * {
        for (const yaml::Node::Member &m : root->members) {
            if (m.key == key) {
                return m.value.get();
            }
        }
        return nullptr;
    };

    // Every CEL scalar, by source id.
    struct Scalar {
        std::string             label;
        const yaml::Node        *node;
        cel::ExprUP             parsed;
    };
    std::vector<Scalar> scalars;
    yaml::LineIndex lines(text);
    struct Err {
        std::string     label;
        uint32_t        offset;
        yaml::Mark      mark;
        std::string     message;
    };
    std::vector<Err> errs;
    auto report = [&](uint32_t source, uint32_t offset, const std::string &msg) {
        const Scalar &sc = scalars[source];
        for (const Err &e : errs) {
            if (e.label == sc.label && e.offset == offset && e.message == msg) {
                return;     // the same type error in a helper, from two of its uses
            }
        }
        errs.push_back({sc.label, offset, lines.at(yaml::sourceOffset(*sc.node, offset)), msg});
    };
    auto parse = [&](const std::string &label, const yaml::Node *n) -> uint32_t {
        uint32_t id = static_cast<uint32_t>(scalars.size());
        scalars.push_back({label, n, nullptr});
        try {
            cel::CelParser p(n->s);
            scalars.back().parsed = p.parse();
        } catch (const cel::SyntaxError &e) {
            report(id, e.offset(), e.what());
        }
        return id;
    };

    cel::CheckEnv env;
    cel::CheckOptions opts;
    std::vector<std::pair<std::string, cel::Value>> values;
    uint64_t max_steps = 0;
    try {
        if (const yaml::Node *v = member("vars")) {
            for (const yaml::Node::Member &m : v->members) {
                env.vars.emplace_back(m.key, cel::Type::parse(m.value->s));
            }
        }
        if (const yaml::Node *b = member("bindings")) {
            for (const yaml::Node::Member &m : b->members) {
                values.emplace_back(m.key, fromYaml(m.value.get()));
                bool declared = false;
                for (const auto &d : env.vars) {
                    declared |= (d.first == m.key);
                }
                if (!declared) {
                    env.vars.emplace_back(m.key, cel::typeOfValue(values.back().second));
                }
            }
        }
    } catch (const std::invalid_argument &e) {
        w.kv("ok", false);
        w.key("error").beginObject().kv("message", e.what()).endObject();
        w.endObject();
        return w.str();
    }
    if (node) {
        values.emplace_back("node", *node);
        bool declared = false;
        for (const auto &d : env.vars) {
            declared |= (d.first == "node");
        }
        if (!declared) {
            env.vars.emplace_back("node", cel::typeOfValue(*node));
        }
    }
    if (const yaml::Node *n = member("max_nesting")) {
        opts.maxNesting = static_cast<int>(n->i);
    }
    if (const yaml::Node *n = member("max_steps")) {
        max_steps = static_cast<uint64_t>(n->i);
    }

    if (const yaml::Node *d = member("define")) {
        for (const yaml::Node::Member &m : d->members) {
            cel::Define def;
            std::string err;
            uint32_t id = parse("define:" + m.key, m.value.get());
            if (!cel::parseDefineHead(m.key, def, err)) {
                report(id, 0, err);
                continue;
            }
            if (!scalars[id].parsed) {
                continue;
            }
            def.body = scalars[id].parsed.get();
            def.source = id;
            env.defines.push_back(def);
        }
    }

    cel::CelChecker defs(env, opts);
    for (const cel::CheckError &e : defs.checkDefines()) {
        report(e.source, e.offset, e.message);
    }

    // let: in order, each a variable for the ones after it; then the rule's
    // expressions.
    struct Checked {
        std::string                     key;
        uint32_t                        source;
        cel::CheckResult                res;
        bool                            let;
    };
    std::vector<Checked> checked;
    auto checkOne = [&](const std::string &key, const yaml::Node *n, const cel::Type *expected,
            bool let) {
        uint32_t id = parse(key, n);
        if (!scalars[id].parsed) {
            return;
        }
        cel::CelChecker c(env, opts);
        std::string what = key;
        cel::CheckResult r = c.check(scalars[id].parsed.get(), id, expected, what.c_str());
        for (const cel::CheckError &e : r.errors) {
            report(e.source, e.offset, e.message);
        }
        if (let) {
            env.vars.emplace_back(key.substr(4), r.type);
        }
        checked.push_back({key, id, std::move(r), let});
    };
    if (const yaml::Node *l = member("let")) {
        for (const yaml::Node::Member &m : l->members) {
            checkOne("let:" + m.key, m.value.get(), nullptr, true);
        }
    }
    cel::Type expect_t;
    const cel::Type *expect = nullptr;
    if (const yaml::Node *x = member("expect")) {
        expect_t = cel::Type::parse(x->s);
        expect = &expect_t;
    }
    cel::Type boolT(cel::Type::Kind::Bool), nodesT = cel::Type::list(cel::Type::node(0));
    if (const yaml::Node *x = member("expr")) {
        checkOne("expr", x, expect, false);
    }
    if (const yaml::Node *x = member("where")) {
        checkOne("where", x, &boolT, false);
    }
    if (const yaml::Node *x = member("report")) {
        checkOne("report", x, &nodesT, false);
    }

    w.kv("ok", true);
    w.key("errors").beginArray();
    for (const Err &e : errs) {
        w.beginObject().kv("at", e.label).kv("offset", e.offset);
        w.kv("line", e.mark.line).kv("col", e.mark.col).kv("message", e.message).endObject();
    }
    w.endArray();
    w.key("types").beginObject();
    for (const Checked &c : checked) {
        w.kv(c.key, c.res.type.str());
    }
    w.endObject();
    w.key("overloads").beginObject();
    for (const Checked &c : checked) {
        w.key(c.key).beginArray();
        for (const cel::Overload *o : c.res.overloads) {
            w.value(o->id);
        }
        w.endArray();
    }
    w.endObject();

    // Run the checked expressions, when everything checked.
    if (errs.empty() && !(member("run") && !member("run")->b)) {
        cel::EvalOptions eopts;
        if (max_steps) {
            eopts.maxSteps = max_steps;
        }
        eopts.host = host;
        w.key("values").beginObject();
        std::vector<std::pair<std::string, cel::Value>> vars = values;
        std::vector<std::pair<std::string, std::vector<std::string>>> calls;
        std::vector<std::pair<std::string, uint64_t>> steps;
        for (const Checked &c : checked) {
            if (!c.res.expr) {
                continue;
            }
            cel::CelEval ev(eopts);
            std::vector<const cel::Overload *> trace;
            ev.setTrace(&trace);
            cel::Value v = ev.eval(c.res.expr.get(), vars);
            w.key(c.key);
            writeValue(w, v);
            if (c.let) {
                vars.emplace_back(c.key.substr(4), v);
            }
            std::vector<std::string> ids;
            for (const cel::Overload *o : trace) {
                ids.push_back(o->id);
            }
            calls.emplace_back(c.key, ids);
            steps.emplace_back(c.key, ev.steps());
        }
        w.endObject();
        w.key("calls").beginObject();
        for (const auto &c : calls) {
            w.key(c.first).beginArray();
            for (const std::string &id : c.second) {
                w.value(id);
            }
            w.endArray();
        }
        w.endObject();
        w.key("steps").beginObject();
        for (const auto &s : steps) {
            w.kv(s.first, s.second);
        }
        w.endObject();
    }
    w.endObject();
    return w.str();
}

std::string LintTestHooks::celCheck(const std::string &name, const std::string &text) {
    return checkDoc(name, text, nullptr);
}

std::string LintTestHooks::reflectSchema() {
    JsonWriter w;
    w.beginObject();
    w.key("classes").beginArray();
    for (uint16_t k=0; k<=rf::NumClasses; k++) {
        const rf::ClassInfo &c = rf::classes[k];
        w.beginObject().kv("name", c.name).kv("super", static_cast<uint64_t>(c.super));
        w.key("fields").beginArray();
        for (uint16_t f=c.firstField; f<c.firstField+c.numFields; f++) {
            const rf::FieldInfo &fi = rf::fields[f];
            w.beginObject().kv("name", fi.name).kv("type", fieldTypeName(fi.type));
            if (fi.type == rf::FieldType::List) {
                w.kv("elem", fieldTypeName(fi.elem));
            }
            w.kv("target", static_cast<uint64_t>(fi.target)).kv("owned", fi.owned);
            w.endObject();
        }
        w.endArray();
        w.endObject();
    }
    w.endArray();
    w.key("enums").beginArray();
    for (uint16_t e=0; e<rf::numEnums; e++) {
        const rf::EnumInfo &ei = rf::enums[e];
        w.beginObject().kv("name", ei.name).kv("flags", ei.flags);
        w.key("values").beginArray();
        for (uint16_t v=0; v<ei.numValues; v++) {
            w.value(ei.values[v]);
        }
        w.endArray().endObject();
    }
    w.endArray();
    w.key("structs").beginArray();
    for (uint16_t s=0; s<rf::numStructs; s++) {
        const rf::StructInfo &si = rf::structs[s];
        w.beginObject().kv("name", si.name);
        w.key("fields").beginArray();
        for (uint16_t f=0; f<si.numFields; f++) {
            w.beginObject().kv("name", si.fields[f].name)
                .kv("type", fieldTypeName(si.fields[f].type)).endObject();
        }
        w.endArray().endObject();
    }
    w.endArray();
    w.endObject();
    return w.str();
}

bool LintTestHooks::parse(const std::string &source, HookTrees &out, std::string &result) {
    std::string error;
    if (!m_parser) {
        error = "no host parser for the node hooks";
    } else if (m_parser(source, out, error) && !out.roots.empty()) {
        if (!out.context) {
            out.context.reset(new run::Context());
        }
        for (const rf::Node &r : out.roots) {
            out.context->index(r);
        }
        return true;
    } else if (error.empty()) {
        error = "the source has no tree";
    }
    JsonWriter w;
    w.beginObject().kv("ok", false).kv("error", error).endObject();
    result = w.str();
    return false;
}

std::string LintTestHooks::celEvalNode(const std::string &source, const std::string &text,
        const std::string &bindings, uint64_t max_steps) {
    HookTrees t;
    std::string result;
    if (!parse(source, t, result)) {
        return result;
    }
    std::vector<std::pair<std::string, cel::Value>> vars = parseBindings(bindings);
    cel::NodeRef ref;
    ref.kind = t.roots[0].kind;
    ref.ptr = t.roots[0].ptr;
    vars.emplace_back("node", cel::Value::mkNode(ref));
    return evalWith(text, vars, max_steps, t.context.get());
}

std::string LintTestHooks::celCheckNode(const std::string &source, const std::string &name,
        const std::string &text) {
    HookTrees t;
    std::string result;
    if (!parse(source, t, result)) {
        return result;
    }
    cel::NodeRef ref;
    ref.kind = t.roots[0].kind;
    ref.ptr = t.roots[0].ptr;
    cel::Value v = cel::Value::mkNode(ref);
    return checkDoc(name, text, &v, t.context.get());
}

std::string LintTestHooks::lintWalk(const std::string &source, const std::string &name,
        const std::string &text) {
    HookTrees t;
    std::string result;
    if (!parse(source, t, result)) {
        return result;
    }
    LintEngine e;
    e.setReader(rules::diskReader());
    LintSource src;
    src.name = name;
    src.text = text;
    bool loaded = e.loadRules(src, false) != nullptr;
    run::Dispatch dispatch(e.ruleList());
    run::Walker walker(dispatch);
    std::vector<std::pair<const rules::Rule *, rf::Node>> matches;
    for (const rf::Node &r : t.roots) {
        walker.walk(r, [&matches](const rules::Rule &rule, const rf::Node &n) {
            matches.emplace_back(&rule, n);
        });
    }

    JsonWriter w;
    w.beginObject();
    w.kv("loaded", loaded);
    w.key("diagnostics");
    rules::diagnosticsToJson(w, e.diagnostics());
    w.kv("visited", walker.numVisited());
    w.key("matches").beginArray();
    for (const auto &m : matches) {
        int32_t fileid = -1, line = -1, col = -1;
        run::Walker::location(m.second, fileid, line, col);
        w.beginObject();
        w.kv("rule", m.first->m_id).kv("kind", rf::classes[m.second.kind].name);
        w.kv("line", static_cast<int64_t>(line)).kv("col", static_cast<int64_t>(col));
        w.key("ancestors").beginArray();
        for (const rf::Node &a : walker.ancestorsOf(m.second)) {
            w.value(rf::classes[a.kind].name);
        }
        w.endArray();
        w.endObject();
    }
    w.endArray();
    w.endObject();
    return w.str();
}

std::string LintTestHooks::lintRun(const std::string &source, const std::string &name,
        const std::string &text, bool allEnabled, const std::vector<std::string> &enabled,
        uint64_t maxSteps, uint32_t gateErrors) {
    static const char *sevNames[] = {"error", "warning", "info", "hint"};
    HookTrees t;
    std::string result;
    if (!parse(source, t, result)) {
        return result;
    }
    LintEngine e;
    e.setReader(rules::diskReader());
    LintSource src;
    src.name = name;
    src.text = text;
    bool loaded = e.loadRules(src, false) != nullptr;
    std::vector<const rules::Rule *> list = e.ruleList();
    std::vector<const rules::Rule *> on;
    for (const rules::Rule *r : list) {
        if (allEnabled || std::find(enabled.begin(), enabled.end(), r->m_id) != enabled.end()) {
            on.push_back(r);
        }
    }
    run::RunOptions opts;
    if (maxSteps) {
        opts.maxSteps = maxSteps;
    }
    if (!t.context) {
        t.context.reset(new run::Context());
    }
    opts.context = t.context.get();
    // The runner sets the context's file names from opts: keep the host's.
    opts.fileNames = t.context->filePaths();
    run::Runner runner(on, opts);
    std::unique_ptr<IMarker> skipped;
    run::Contexts contexts;
    if (gateErrors) {
        skipped.reset(run::Runner::skippedMarker(gateErrors));
    } else {
        for (const rf::Node &r : t.roots) {
            runner.index(r);
        }
        std::vector<const rules::Classifier *> cls = e.classifiers();
        if (!cls.empty()) {
            contexts = run::classify(cls, *t.context);
        }
        for (const rf::Node &r : t.roots) {
            runner.run(r);
        }
    }

    JsonWriter w;
    w.beginObject();
    w.kv("loaded", loaded);
    w.key("diagnostics");
    rules::diagnosticsToJson(w, e.diagnostics());
    w.key("skipped");
    if (skipped) {
        w.beginObject();
        w.kv("id", skipped->id()).kv("severity", sevNames[static_cast<int>(skipped->severity())]);
        w.kv("msg", skipped->msg()).kv("line", static_cast<int64_t>(skipped->loc().lineno));
        w.endObject();
    } else {
        w.valueNull();
    }
    w.key("findings").beginArray();
    for (const run::Finding &f : runner.findings()) {
        w.beginObject();
        w.kv("id", f.rule->m_id).kv("severity", sevNames[static_cast<int>(f.severity)]);
        w.kv("msg", f.message).kv("kind", rf::classes[f.node.kind].name);
        w.kv("line", static_cast<int64_t>(f.loc.lineno));
        w.kv("col", static_cast<int64_t>(f.loc.linepos));
        w.kv("extent", static_cast<int64_t>(f.loc.extent));
        w.endObject();
    }
    w.endArray();
    w.key("failures").beginArray();
    for (const run::RuleFailure &f : runner.failures()) {
        std::unique_ptr<IMarker> m(run::Runner::toMarker(f));
        w.beginObject();
        w.kv("id", m->id()).kv("rule", f.rule->m_id).kv("in", f.in).kv("error", f.error);
        w.kv("msg", m->msg()).kv("severity", sevNames[static_cast<int>(m->severity())]);
        w.kv("line", static_cast<int64_t>(m->loc().lineno));
        w.kv("col", static_cast<int64_t>(m->loc().linepos));
        w.endObject();
    }
    w.endArray();
    w.key("notes").beginArray();
    for (const run::RuleNote &n : runner.notes()) {
        std::unique_ptr<IMarker> m(run::Runner::toMarker(n));
        w.beginObject();
        w.kv("id", m->id()).kv("rule", n.rule ? n.rule->m_id : std::string()).kv("path", n.path);
        w.kv("msg", m->msg()).kv("severity", sevNames[static_cast<int>(m->severity())]);
        w.kv("line", static_cast<int64_t>(m->loc().lineno));
        w.kv("col", static_cast<int64_t>(m->loc().linepos));
        w.endObject();
    }
    w.endArray();
    // The classify stage (run/Classify.h): each label, and what it noted.
    auto lineOf = [](const rf::Node &n) {
        ast::Location loc;
        return (n && run::Walker::location(n, loc)) ? static_cast<int64_t>(loc.lineno) : -1;
    };
    w.key("contexts").beginArray();
    for (const run::ContextLabel &l : contexts.labels) {
        w.beginObject();
        w.kv("kind", rf::classes[l.carrier.kind].name).kv("line", lineOf(l.carrier));
        w.kv("axis", l.axis).kv("label", l.label).kv("unit", l.unit);
        w.kv("source", run::sourceName(l.source)).kv("evidence", l.evidence);
        w.endObject();
    }
    w.endArray();
    w.key("context_notes").beginArray();
    for (const run::ContextNote &n : contexts.notes) {
        w.beginObject().kv("line", lineOf(n.carrier)).kv("message", n.message).endObject();
    }
    w.endArray();
    w.key("stats").beginArray();
    for (const rules::Rule *r : list) {
        // A rule not enabled was never given to the runner: all zero.
        auto it = std::find(on.begin(), on.end(), r);
        run::RuleStats none;
        const run::RuleStats &s = (it == on.end()) ? none : runner.stats()[it - on.begin()];
        w.beginObject();
        w.kv("rule", r->m_id).kv("matches", s.matches).kv("findings", s.findings);
        w.kv("steps", s.steps).kv("ns", s.ns).kv("errors", s.errors);
        w.kv("error_in", s.errorIn).kv("error", s.error);
        w.endObject();
    }
    w.endArray();
    w.endObject();
    return w.str();
}

std::string LintTestHooks::lintExamples(const std::string &name, const std::string &text,
        bool allEnabled, const std::vector<std::string> &enabled) {
    if (!m_compiler) {
        JsonWriter w;
        w.beginObject().kv("ok", false).kv("error", "the host has no compiler for examples")
            .endObject();
        return w.str();
    }
    LintEngine e;
    e.setReader(rules::diskReader());
    LintSource src;
    src.name = name;
    src.text = text;
    bool loaded = e.loadRules(src, false) != nullptr;

    JsonWriter w;
    w.beginObject();
    w.kv("ok", true).kv("loaded", loaded);
    w.key("diagnostics");
    rules::diagnosticsToJson(w, e.diagnostics());
    w.key("results").beginArray();
    for (const rules::Rule *r : e.ruleList()) {
        if (!allEnabled && std::find(enabled.begin(), enabled.end(), r->m_id) == enabled.end()) {
            continue;
        }
        for (const rules::Example &x : r->m_examples) {
            run::ExampleResult res = run::runExample(*r, x, m_compiler, e.classifiers());
            w.beginObject();
            w.kv("rule", res.rule).kv("name", res.name).kv("file", res.file);
            w.kv("line", static_cast<int64_t>(res.line)).kv("col", static_cast<int64_t>(res.col));
            w.kv("ok", res.ok()).kv("describe", res.describe());
            w.key("problems").beginArray();
            for (const std::string &p : res.problems) {
                w.value(p);
            }
            w.endArray();
            w.key("markers").beginArray();
            for (const run::ExampleMarker &m : res.markers) {
                w.beginObject();
                w.kv("code", m.code).kv("file", m.file).kv("line", static_cast<int64_t>(m.line));
                w.kv("col", static_cast<int64_t>(m.col)).kv("message", m.message);
                w.endObject();
            }
            w.endArray();
            w.endObject();
        }
    }
    w.endArray();
    w.endObject();
    return w.str();
}

std::string LintTestHooks::ruleLoad(const std::string &name, const std::string &text,
        bool builtin) {
    LintEngine e;
    e.setReader(rules::diskReader());
    LintSource src;
    src.name = name;
    src.text = text;
    bool loaded = e.loadRules(src, builtin) != nullptr;
    return "{\"loaded\":" + std::string(loaded ? "true" : "false") + ",\"rules\":" + e.rulesJson()
        + ",\"diagnostics\":" + e.diagnosticsJson() + "}";
}

std::string LintTestHooks::rulesSchema() {
    return rules::rulesSchemaJson();
}

std::string LintTestHooks::regexSearch(const std::string &pattern, const std::string &text) {
    JsonWriter w;
    w.beginObject();
    try {
        std::unique_ptr<regex::Regex> re(regex::Regex::compile(pattern));
        uint64_t steps = 0;
        bool m = re->search(text, &steps);
        w.kv("ok", true).kv("match", m).kv("steps", steps)
            .kv("size", static_cast<uint64_t>(re->size()));
    } catch (const regex::RegexError &e) {
        w.kv("ok", false);
        w.key("error").beginObject().kv("offset", e.offset()).kv("message", e.what()).endObject();
    }
    w.endObject();
    return w.str();
}

std::string LintTestHooks::dispatch(const std::string &requestJson) {
    using NK = yaml::Node::Kind;
    auto refuse = [](const std::string &msg) {
        JsonWriter w;
        w.beginObject().kv("ok", false).kv("error", msg).endObject();
        return w.str();
    };
    yaml::NodeUP req;
    try {
        req = yaml::parse("<hook request>", requestJson);
    } catch (const yaml::YamlError &e) {
        return refuse(e.what());
    }
    const yaml::Node *op = req ? req->get("op") : nullptr;
    const yaml::Node *args = req ? req->get("args") : nullptr;
    if (!op || op->kind != NK::String) {
        return refuse("the request has no \"op\"");
    }
    std::vector<const yaml::Node *> a;
    if (args && args->kind == NK::Seq) {
        for (const yaml::NodeUP &n : args->items) {
            a.push_back(n.get());
        }
    } else if (args && args->kind != NK::Null) {
        return refuse("\"args\" is not a list");
    }
    // The argument kinds a hook takes: s(tring), i(nt), b(ool).
    auto fits = [&](const char *sig) {
        if (a.size() != std::strlen(sig)) {
            return false;
        }
        for (size_t i=0; i<a.size(); i++) {
            NK want = (sig[i] == 's') ? NK::String : (sig[i] == 'i') ? NK::Int : NK::Bool;
            if (a[i]->kind != want) {
                return false;
            }
        }
        return true;
    };
    auto str = [&](size_t i) { return a[i]->s; };
    const std::string &o = op->s;
    if (o == "yamlLoad" && fits("ss")) {
        return yamlLoad(str(0), str(1));
    } else if (o == "yamlSourceMaps" && fits("ss")) {
        return yamlSourceMaps(str(0), str(1));
    } else if (o == "yamlCelParse" && fits("sss")) {
        return yamlCelParse(str(0), str(1), str(2));
    } else if (o == "celParse" && fits("s")) {
        return celParse(str(0));
    } else if (o == "celEval" && fits("ssi")) {
        return celEval(str(0), str(1), static_cast<uint64_t>(a[2]->i));
    } else if (o == "celFunctions" && fits("")) {
        return celFunctions();
    } else if (o == "celCheck" && fits("ss")) {
        return celCheck(str(0), str(1));
    } else if (o == "ruleLoad" && fits("ssb")) {
        return ruleLoad(str(0), str(1), a[2]->b);
    } else if (o == "rulesSchema" && fits("")) {
        return rulesSchema();
    } else if (o == "regexSearch" && fits("ss")) {
        return regexSearch(str(0), str(1));
    } else if (o == "reflectSchema" && fits("")) {
        return reflectSchema();
    } else if (o == "celEvalNode" && fits("sssi")) {
        return celEvalNode(str(0), str(1), str(2), static_cast<uint64_t>(a[3]->i));
    } else if (o == "celCheckNode" && fits("sss")) {
        return celCheckNode(str(0), str(1), str(2));
    } else if (o == "lintWalk" && fits("sss")) {
        return lintWalk(str(0), str(1), str(2));
    } else if (o == "lintRun" && a.size() == 7 && a[0]->kind == NK::String
            && a[1]->kind == NK::String && a[2]->kind == NK::String && a[3]->kind == NK::Bool
            && a[4]->kind == NK::Seq && a[5]->kind == NK::Int && a[6]->kind == NK::Int) {
        std::vector<std::string> enabled;
        for (const yaml::NodeUP &n : a[4]->items) {
            if (n->kind != NK::String) {
                return refuse("lintRun: `enabled` is a list of strings");
            }
            enabled.push_back(n->s);
        }
        return lintRun(str(0), str(1), str(2), a[3]->b, enabled,
            static_cast<uint64_t>(a[5]->i), static_cast<uint32_t>(a[6]->i));
    } else if (o == "lintExamples" && a.size() == 4 && a[0]->kind == NK::String
            && a[1]->kind == NK::String && a[2]->kind == NK::Bool && a[3]->kind == NK::Seq) {
        std::vector<std::string> enabled;
        for (const yaml::NodeUP &n : a[3]->items) {
            if (n->kind != NK::String) {
                return refuse("lintExamples: `enabled` is a list of strings");
            }
            enabled.push_back(n->s);
        }
        return lintExamples(str(0), str(1), a[2]->b, enabled);
    } else if (o == "engineNew" && fits("")) {
        return engineNew();
    } else if (o == "engineLoadRules" && fits("issb")) {
        return engineLoadRules(a[0]->i, str(1), str(2), a[3]->b);
    } else if (o == "engineRules" && fits("i")) {
        return engineRules(a[0]->i);
    } else if (o == "engineDiagnostics" && fits("i")) {
        return engineDiagnostics(a[0]->i);
    } else if (o == "engineInfo" && fits("i")) {
        return engineInfo(a[0]->i);
    } else if (o == "engineRun" && fits("iss")) {
        return engineRun(a[0]->i, str(1), str(2));
    }
    return refuse("no hook '" + o + "' taking these arguments");
}

LintEngine *LintTestHooks::engine(int64_t id) {
    return (id >= 0 && static_cast<size_t>(id) < m_engines.size()) ? m_engines[id].get()
        : nullptr;
}

namespace {

std::string noEngine(int64_t id) {
    JsonWriter w;
    w.beginObject().kv("ok", false).kv("error", "no engine " + std::to_string(id)).endObject();
    return w.str();
}

}

std::string LintTestHooks::engineNew() {
    m_engines.emplace_back(new LintEngine());
    m_engines.back()->setReader(rules::diskReader());
    JsonWriter w;
    w.beginObject().kv("id", static_cast<int64_t>(m_engines.size() - 1)).endObject();
    return w.str();
}

std::string LintTestHooks::engineLoadRules(int64_t id, const std::string &name,
        const std::string &text, bool builtin) {
    LintEngine *e = engine(id);
    if (!e) {
        return noEngine(id);
    }
    LintSource src;
    src.name = name;
    src.text = text;
    JsonWriter w;
    w.beginObject().kv("loaded", e->loadRules(src, builtin) != nullptr).endObject();
    return w.str();
}

std::string LintTestHooks::engineRules(int64_t id) {
    LintEngine *e = engine(id);
    return e ? e->rulesJson() : noEngine(id);
}

std::string LintTestHooks::engineDiagnostics(int64_t id) {
    LintEngine *e = engine(id);
    return e ? e->diagnosticsJson() : noEngine(id);
}

std::string LintTestHooks::engineInfo(int64_t id) {
    LintEngine *e = engine(id);
    if (!e) {
        return noEngine(id);
    }
    JsonWriter w;
    w.beginObject().kv("format_version", static_cast<int64_t>(e->ruleFormatVersion()));
    w.kv("rule_sets", static_cast<int64_t>(e->numRuleSets())).endObject();
    return w.str();
}

std::string LintTestHooks::engineRun(int64_t id, const std::string &source,
        const std::string &request) {
    LintEngine *e = engine(id);
    if (!e) {
        return noEngine(id);
    }
    HookTrees t;
    std::string result;
    if (!parse(source, t, result)) {
        return result;
    }
    return e->run(t.roots, request, t.context.get());
}

}
}
