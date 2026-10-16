/*
 * Schema.cpp
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
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>
#include "Schema.h"
#include "SemModel.h"
#include "Syntax.h"

namespace svcheck {
namespace host {

namespace {

SemanticLayer s_layer = SemanticLayer::None;

// The merged tables; they live as long as the process (install() keeps
// pointers to them).
struct Merged {
    std::vector<rf::ClassInfo>                  classes;
    std::vector<rf::FieldInfo>                  fields;
    std::vector<rf::EnumInfo>                   enums;
    std::vector<rf::StructInfo>                 structs;
    std::vector<std::vector<rf::StructFieldInfo>>   structFields;
};

Merged s_merged;
const rf::Schema *s_first = nullptr;
const rf::Schema *s_second = nullptr;

bool inFirst(const rf::Node &n) {
    return s_first && n.kind >= 1 && n.kind <= s_first->numClasses;
}

bool location(const rf::Node &n, ast::Location &loc) {
    const rf::Schema *s = inFirst(n) ? s_first : s_second;
    if (!s || !s->location) {
        loc = ast::Location();
        return false;
    }
    return s->location(n, loc);
}

bool nameLocation(const rf::Node &n, ast::Location &loc) {
    const rf::Schema *s = inFirst(n) ? s_first : s_second;
    if (!s || !s->nameLocation) {
        loc = ast::Location();
        return false;
    }
    return s->nameLocation(n, loc);
}

uint16_t shiftedTarget(const rf::FieldInfo &f, uint16_t cls, uint16_t enm, uint16_t st) {
    rf::FieldType t = (f.type == rf::FieldType::List) ? f.elem : f.type;
    switch (t) {
    case rf::FieldType::Node: return f.target + cls;
    case rf::FieldType::Enum:
    case rf::FieldType::Flags: return f.target + enm;
    case rf::FieldType::Struct: return f.target + st;
    default: return f.target;
    }
}

// Appends `s` to the merged tables, its indexes shifted past what is there.
void append(const rf::Schema &s) {
    Merged &m = s_merged;
    uint16_t cls = static_cast<uint16_t>(m.classes.size() - 1);
    uint16_t fld = static_cast<uint16_t>(m.fields.size());
    uint16_t enm = static_cast<uint16_t>(m.enums.size());
    uint16_t st = static_cast<uint16_t>(m.structs.size());
    for (uint16_t i=1; i<=s.numClasses; i++) {
        rf::ClassInfo c = s.classes[i];
        if (c.super) {
            c.super += cls;
        }
        c.firstField += fld;
        m.classes.push_back(c);
    }
    for (uint16_t i=0; i<s.numFields; i++) {
        rf::FieldInfo f = s.fields[i];
        f.owner += cls;
        f.target = shiftedTarget(f, cls, enm, st);
        m.fields.push_back(f);
    }
    for (uint16_t i=0; i<s.numEnums; i++) {
        m.enums.push_back(s.enums[i]);
    }
    for (uint16_t i=0; i<s.numStructs; i++) {
        const rf::StructInfo &si = s.structs[i];
        std::vector<rf::StructFieldInfo> sf(si.fields, si.fields + si.numFields);
        for (rf::StructFieldInfo &f : sf) {
            if (f.type == rf::FieldType::Enum || f.type == rf::FieldType::Flags) {
                f.target += enm;
            }
        }
        m.structFields.push_back(std::move(sf));
        m.structs.push_back(rf::StructInfo{si.name, si.numFields, nullptr});
    }
}

}

bool semanticLayerOf(const std::string &name, SemanticLayer &layer) {
    if (name == "none") {
        layer = SemanticLayer::None;
    } else if (name == "model") {
        layer = SemanticLayer::Model;
    } else {
        return false;
    }
    return true;
}

void installSchema(SemanticLayer layer) {
    if (rf::installed()) {
        return;
    }
    s_layer = layer;
    switch (layer) {
    case SemanticLayer::Model: s_first = &semModelSchema(); break;
    case SemanticLayer::None: s_first = nullptr; break;
    }
    s_second = &syntaxSchema();

    Merged &m = s_merged;
    m.classes.push_back(rf::ClassInfo{"", 0, 0, 0});
    if (s_first) {
        append(*s_first);
    }
    setSyntaxBases(static_cast<uint16_t>(m.classes.size() - 1),
        static_cast<uint16_t>(m.fields.size()), static_cast<uint16_t>(m.enums.size()),
        static_cast<uint16_t>(m.structs.size()));
    append(*s_second);
    for (size_t i=0; i<m.structs.size(); i++) {
        m.structs[i].fields = m.structFields[i].data();
    }

    std::set<std::string> names;
    for (size_t i=1; i<m.classes.size(); i++) {
        if (!names.insert(m.classes[i].name).second) {
            std::fprintf(stderr, "svcheck: class name '%s' is in both the semantic layer "
                "and the syntax schema\n", m.classes[i].name);
            std::abort();
        }
    }

    rf::Schema s;
    s.numClasses = static_cast<uint16_t>(m.classes.size() - 1);
    s.classes = m.classes.data();
    s.fields = m.fields.data();
    s.numFields = static_cast<uint16_t>(m.fields.size());
    s.enums = m.enums.data();
    s.numEnums = static_cast<uint16_t>(m.enums.size());
    s.structs = m.structs.data();
    s.numStructs = static_cast<uint16_t>(m.structs.size());
    s.location = &location;
    s.nameLocation = &nameLocation;
    rf::install(s);
}

SemanticLayer installedLayer() {
    return s_layer;
}

bool isSemanticClass(uint16_t kind) {
    return s_first && kind >= 1 && kind <= s_first->numClasses;
}

}
}
