/*
 * Reflect.h
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
 * The node schema the engine reads (svcheck-plan P1.1, pssparser C9.1).
 *
 * In pssparser this header is generated (pyastbuilder gen-reflect) with the
 * class, field, enum and struct tables as compile-time constants. Here the
 * types are the same, but the tables are the host's: it fills a Schema and
 * install()s it before any rule is loaded, and the engine reads it through
 * the same names (NumClasses, classes[], fields[], ...), which are variables
 * rather than constants. Until a host installs one, the schema is empty:
 * class 0 ("no node") only, which is all the CEL, regex and YAML layers
 * need.
 */
#pragma once
#include <stdint.h>
#include <string>
#include <string_view>
#include "svcheck/ast/Location.h"

namespace svcheck {
namespace ast {
namespace reflect {

/**
 * A node: its class id (1..NumClasses; 0 is "no node") and the host's
 * pointer to it. Only the host's accessors (FieldInfo) look behind `ptr`.
 */
struct Node {
    uint16_t    kind = 0;
    void        *ptr = nullptr;

    explicit operator bool() const { return ptr != nullptr; }
    bool operator==(const Node &o) const { return kind == o.kind && ptr == o.ptr; }
    bool operator!=(const Node &o) const { return !(*this == o); }
};

enum class FieldType : uint8_t {
    Bool, I8, U8, I16, U16, I32, U32, I64, U64, F32, F64, String,
    Enum, Flags, Struct, Node, List, Map
};

struct Value {
    enum class Kind : uint8_t { Absent, Bool, Int, Uint, Float, String, Enum, Flags, Struct, Node, List };

    Kind                kind = Kind::Absent;
    bool                borrowed = false;
    uint16_t            type = 0;           // Enum, Flags: enum index; Struct: struct index; List: element FieldType
    union {
        bool                b;
        int64_t             i;              // Int, Enum, Flags
        uint64_t            u;
        double              d;
        struct {
            const char      *data;
            size_t          size;
        }                   s;              // String: the text, owned by the node
        const void          *st;            // Struct, List: owned by the node
        Node                n;              // Node: kind 0 for a null pointer
    };

    Value() : n() { }

    static Value ofBool(bool v) { Value r; r.kind = Kind::Bool; r.b = v; return r; }
    static Value ofInt(int64_t v) { Value r; r.kind = Kind::Int; r.i = v; return r; }
    static Value ofUint(uint64_t v) { Value r; r.kind = Kind::Uint; r.u = v; return r; }
    static Value ofFloat(double v) { Value r; r.kind = Kind::Float; r.d = v; return r; }
    static Value ofString(std::string_view v) {
        Value r; r.kind = Kind::String; r.s.data = v.data(); r.s.size = v.size(); return r;
    }
    static Value ofString(const std::string *v) { return ofString(std::string_view(*v)); }
    static Value ofEnum(int64_t v, uint16_t t) { Value r; r.kind = Kind::Enum; r.i = v; r.type = t; return r; }
    static Value ofFlags(int64_t v, uint16_t t) { Value r; r.kind = Kind::Flags; r.i = v; r.type = t; return r; }
    static Value ofStruct(const void *v, uint16_t t) { Value r; r.kind = Kind::Struct; r.st = v; r.type = t; return r; }
    static Value ofNode(Node v, bool borrowed=false) {
        Value r; r.kind = Kind::Node; r.n = v; r.borrowed = borrowed; return r;
    }
    /** String: the text. */
    std::string_view str() const { return std::string_view(s.data, s.size); }

    template <class T> static Value ofList(const T *v, FieldType elem) {
        Value r; r.kind = Kind::List; r.st = v; r.type = static_cast<uint16_t>(elem); return r;
    }
};

/**
 * A scalar list value (List, `st` a std::vector of the element type): its
 * size and elements. A list of nodes is read through its field's `size` and
 * `at` instead.
 */
uint32_t listSize(const Value &list);
Value listAt(const Value &list, uint32_t idx);

struct FieldInfo {
    const char  *name;
    uint16_t    owner;      // the declaring class
    FieldType   type;
    FieldType   elem;       // List: the element type
    uint16_t    target;     // Node: the declared class; Enum, Flags: enum index; Struct: struct index
    bool        owned;      // Node: an owning pointer (a child) rather than a reference
    Value       (*get)(void *obj, uint16_t kind);
    uint32_t    (*size)(void *obj, uint16_t kind);
    Value       (*at)(void *obj, uint16_t kind, uint32_t idx);
};

struct ClassInfo {
    const char  *name;
    uint16_t    super;          // 0 for a root class
    uint16_t    firstField;     // index into fields[] of the first declared field
    uint16_t    numFields;      // declared here, not inherited
};

struct EnumInfo {
    const char          *name;
    bool                flags;
    uint16_t            numValues;
    const char *const   *values;
};

struct StructFieldInfo {
    const char  *name;
    FieldType   type;
    uint16_t    target;         // Enum, Flags: enum index
    Value       (*get)(const void *st);
};

struct StructInfo {
    const char              *name;
    uint16_t                numFields;
    const StructFieldInfo   *fields;
};

/**
 * A host's tables, in gen-reflect's layout: `classes` has numClasses + 1
 * entries, [0] being "no node"; a class's fields are contiguous in `fields`
 * and its super class has a lower id.
 */
struct Schema {
    uint16_t                numClasses = 0;
    const ClassInfo         *classes = nullptr;
    const FieldInfo         *fields = nullptr;
    uint16_t                numFields = 0;
    const EnumInfo          *enums = nullptr;
    uint16_t                numEnums = 0;
    const StructInfo        *structs = nullptr;
    uint16_t                numStructs = 0;

    /**
     * Where a node is (1-based line and column), for findings and the
     * location functions; false when it has none. Unset: a `location`
     * struct field ({fileid, lineno, linepos, extent}, as pyastbuilder
     * models carry), or none.
     */
    bool                    (*location)(const Node &n, Location &loc) = nullptr;

    /**
     * For a named declaration, where its name is (a finding on a
     * declaration points at the name); false for other nodes. Unset: never.
     */
    bool                    (*nameLocation)(const Node &n, Location &loc) = nullptr;
};

/**
 * Makes `s` the schema. Call once, before any rule is loaded or node
 * evaluated: the engine's indexes over the tables are built on first use
 * and not rebuilt. The tables must outlive the engine.
 */
void install(const Schema &s);

/** Whether a host has installed a schema. */
bool installed();

/** The installed schema (its hooks; the tables are also the variables below). */
const Schema &schema();

extern uint16_t             NumClasses;
extern const ClassInfo      *classes;       // [NumClasses + 1]
extern const FieldInfo      *fields;
extern uint16_t             numFields;
extern const EnumInfo       *enums;
extern uint16_t             numEnums;
extern const StructInfo     *structs;
extern uint16_t             numStructs;

} // namespace reflect
} // namespace ast
} // namespace svcheck
