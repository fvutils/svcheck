/*
 * Syntax.cpp
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
 * The hand-written half of the syntax schema: the Token struct, the
 * TokenKind enum, node values and locations. The class and field tables
 * are generated (SyntaxSchema.cpp).
 */
#include <string>
#include <vector>
#include "Syntax.h"
#include "SyntaxValues.h"
#include "slang/text/SourceManager.h"

namespace svcheck {
namespace host {

namespace syntax {

namespace {

Sources *s_sources = nullptr;

const slang::parsing::Token &tok(const void *st) {
    return *static_cast<const slang::parsing::Token *>(st);
}

bool tokenLocation(const slang::parsing::Token &t, ast::Location &loc) {
    loc = ast::Location();
    return s_sources && s_sources->locate(t.range(), loc);
}

rf::Value tokText(const void *st) { return rf::Value::ofString(tok(st).valueText()); }
rf::Value tokRaw(const void *st) { return rf::Value::ofString(tok(st).rawText()); }
rf::Value tokKind(const void *st) {
    return rf::Value::ofEnum(static_cast<int64_t>(tok(st).kind), TokenKindEnum);
}
rf::Value tokMissing(const void *st) { return rf::Value::ofBool(tok(st).isMissing()); }
rf::Value tokLine(const void *st) {
    ast::Location loc;
    tokenLocation(tok(st), loc);
    return rf::Value::ofInt(loc.lineno);
}
rf::Value tokCol(const void *st) {
    ast::Location loc;
    tokenLocation(tok(st), loc);
    return rf::Value::ofInt(loc.linepos);
}

const rf::StructFieldInfo tokenFields[] = {
    {"text", rf::FieldType::String, 0, &tokText},
    {"raw", rf::FieldType::String, 0, &tokRaw},
    {"kind", rf::FieldType::Enum, TokenKindEnum, &tokKind},
    {"missing", rf::FieldType::Bool, 0, &tokMissing},
    {"line", rf::FieldType::I32, 0, &tokLine},
    {"col", rf::FieldType::I32, 0, &tokCol},
};

const rf::StructInfo structs[] = {
    {"Token", static_cast<uint16_t>(std::size(tokenFields)), tokenFields},
};

// TokenKind's names, from slang (its enum is in tokenkinds.txt's order).
const std::vector<const char *> &tokenKindNames() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n;
        for (slang::parsing::TokenKind k : slang::parsing::TokenKind_traits::values) {
            n.emplace_back(slang::parsing::toString(k));
        }
        return n;
    }();
    static const std::vector<const char *> ptrs = [] {
        std::vector<const char *> p;
        for (const std::string &s : names) {
            p.push_back(s.c_str());
        }
        return p;
    }();
    return ptrs;
}

// Per class, found through its super classes: its `name` field, a token
// or a node (a declaration's name), or -1; and for a declaration that keeps
// its name one level down (a module's `header`, a function's `prototype`,
// a method's `declaration`), that field, or -1.
std::vector<int32_t> s_nameField;
std::vector<int32_t> s_viaField;

int32_t findField(uint16_t k, const char *name, bool (*fits)(const rf::FieldInfo &)) {
    for (uint16_t c=k; c; c=classes[c].super) {
        const rf::ClassInfo &ci = classes[c];
        for (uint16_t f=ci.firstField; f<ci.firstField+ci.numFields; f++) {
            if (std::string(fields[f].name) == name && fits(fields[f])) {
                return f;
            }
        }
    }
    return -1;
}

bool isNameField(const rf::FieldInfo &f) {
    return (f.type == rf::FieldType::Struct && f.target == TokenStruct)
        || f.type == rf::FieldType::Node;
}

bool isNodeField(const rf::FieldInfo &f) {
    return f.type == rf::FieldType::Node;
}

void buildNameFields() {
    s_nameField.assign(NumClasses + 1, -1);
    s_viaField.assign(NumClasses + 1, -1);
    for (uint16_t k=1; k<=NumClasses; k++) {
        s_nameField[k] = findField(k, "name", &isNameField);
        if (s_nameField[k] >= 0) {
            continue;
        }
        for (const char *via : {"header", "prototype", "declaration"}) {
            if ((s_viaField[k] = findField(k, via, &isNodeField)) >= 0) {
                break;
            }
        }
    }
}

const slang::syntax::SyntaxNode *asSyntax(const rf::Node &n) {
    return static_cast<const slang::syntax::SyntaxNode *>(n.ptr);
}

bool location(const rf::Node &n, ast::Location &loc) {
    loc = ast::Location();
    return n && s_sources && s_sources->locate(asSyntax(n)->sourceRange(), loc);
}

bool nameLocation(const rf::Node &n, ast::Location &loc) {
    loc = ast::Location();
    rf::Node at = n;
    // Two steps at most: a method's declaration, then the function's prototype.
    for (int step=0; step<3 && at && at.kind < s_nameField.size(); step++) {
        if (s_nameField[at.kind] >= 0) {
            rf::Value v = fields[s_nameField[at.kind]].get(at.ptr, at.kind);
            if (v.kind == rf::Value::Kind::Struct) {
                return tokenLocation(tok(v.st), loc);
            }
            return v.kind == rf::Value::Kind::Node && location(v.n, loc);
        }
        if (s_viaField[at.kind] < 0) {
            return false;
        }
        at = fields[s_viaField[at.kind]].get(at.ptr, at.kind).n;
    }
    return false;
}

}

rf::Value nodeValue(const slang::syntax::SyntaxNode *n) {
    return rf::Value::ofNode(host::syntaxNode(n));
}

rf::Value tokenValue(const slang::parsing::Token &t) {
    if (!t) {
        return rf::Value();
    }
    return rf::Value::ofStruct(&t, TokenStruct);
}

}

void installSyntaxSchema() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    static rf::EnumInfo enums[1];
    enums[0].name = "TokenKind";
    enums[0].flags = false;
    enums[0].numValues = static_cast<uint16_t>(syntax::tokenKindNames().size());
    enums[0].values = syntax::tokenKindNames().data();
    syntax::buildNameFields();

    rf::Schema s;
    s.numClasses = syntax::NumClasses;
    s.classes = syntax::classes;
    s.fields = syntax::fields;
    s.numFields = syntax::numFields;
    s.enums = enums;
    s.numEnums = 1;
    s.structs = syntax::structs;
    s.numStructs = static_cast<uint16_t>(std::size(syntax::structs));
    s.location = &syntax::location;
    s.nameLocation = &syntax::nameLocation;
    rf::install(s);
}

rf::Node syntaxNode(const slang::syntax::SyntaxNode *n) {
    rf::Node r;
    if (n) {
        r.kind = syntax::classOf(n->kind);
        r.ptr = const_cast<slang::syntax::SyntaxNode *>(n);
    }
    return r;
}

void setSources(Sources *s) {
    syntax::s_sources = s;
}

int32_t Sources::fileid(slang::BufferID buffer) {
    auto it = m_ids.find(buffer.getId());
    if (it != m_ids.end()) {
        return it->second;
    }
    std::string path = m_sm.getFullPath(buffer).string();
    if (path.empty()) {
        path = std::string(m_sm.getRawFileName(buffer));
    }
    int32_t id = static_cast<int32_t>(m_paths.size());
    m_paths.push_back(path);
    m_ids.emplace(buffer.getId(), id);
    return id;
}

bool Sources::locate(slang::SourceRange range, ast::Location &loc) {
    loc = ast::Location();
    slang::SourceLocation start = m_sm.getFullyExpandedLoc(range.start());
    if (!start.valid()) {
        return false;
    }
    loc.fileid = fileid(start.buffer());
    loc.lineno = static_cast<int32_t>(m_sm.getLineNumber(start));
    loc.linepos = static_cast<int32_t>(m_sm.getColumnNumber(start));
    loc.extent = 0;
    slang::SourceLocation end = m_sm.getFullyExpandedLoc(range.end());
    if (end.valid() && end.buffer() == start.buffer()
            && m_sm.getLineNumber(end) == static_cast<size_t>(loc.lineno)
            && end.offset() >= start.offset()) {
        loc.extent = static_cast<int32_t>(end.offset() - start.offset());
    }
    return loc.lineno > 0;
}

}
}
