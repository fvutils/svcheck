/*
 * Reflect.cpp
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
 * The installed schema, and the scalar list accessors (as pyastbuilder
 * gen-reflect writes them).
 */
#include <vector>
#include "Reflect.h"

namespace svcheck {
namespace ast {
namespace reflect {

namespace {

const ClassInfo NoClasses[1] = { {"", 0, 0, 0} };
bool s_installed = false;

}

uint16_t            NumClasses = 0;
const ClassInfo     *classes = NoClasses;
const FieldInfo     *fields = nullptr;
uint16_t            numFields = 0;
const EnumInfo      *enums = nullptr;
uint16_t            numEnums = 0;
const StructInfo    *structs = nullptr;
uint16_t            numStructs = 0;

void install(const Schema &s) {
    NumClasses = s.numClasses;
    classes = s.classes ? s.classes : NoClasses;
    fields = s.fields;
    numFields = s.numFields;
    enums = s.enums;
    numEnums = s.numEnums;
    structs = s.structs;
    numStructs = s.numStructs;
    s_installed = true;
}

bool installed() {
    return s_installed;
}

namespace {

template <class T> const std::vector<T> &vec(const Value &l) {
    return *static_cast<const std::vector<T> *>(l.st);
}

template <class T> Value elem(const Value &l, uint32_t i, Value (*mk)(T)) {
    const std::vector<T> &v = vec<T>(l);
    return (i < v.size()) ? mk(v[i]) : Value();
}

Value mkBool(bool v) { return Value::ofBool(v); }
Value mkInt(int64_t v) { return Value::ofInt(v); }
Value mkUint(uint64_t v) { return Value::ofUint(v); }
Value mkFloat(double v) { return Value::ofFloat(v); }

}

uint32_t listSize(const Value &l) {
    if (l.kind != Value::Kind::List) { return 0; }
    switch (static_cast<FieldType>(l.type)) {
    case FieldType::Bool: return static_cast<uint32_t>(vec<bool>(l).size());
    case FieldType::I8: return static_cast<uint32_t>(vec<int8_t>(l).size());
    case FieldType::U8: return static_cast<uint32_t>(vec<uint8_t>(l).size());
    case FieldType::I16: return static_cast<uint32_t>(vec<int16_t>(l).size());
    case FieldType::U16: return static_cast<uint32_t>(vec<uint16_t>(l).size());
    case FieldType::I32: return static_cast<uint32_t>(vec<int32_t>(l).size());
    case FieldType::U32: return static_cast<uint32_t>(vec<uint32_t>(l).size());
    case FieldType::I64: return static_cast<uint32_t>(vec<int64_t>(l).size());
    case FieldType::U64: return static_cast<uint32_t>(vec<uint64_t>(l).size());
    case FieldType::F32: return static_cast<uint32_t>(vec<float>(l).size());
    case FieldType::F64: return static_cast<uint32_t>(vec<double>(l).size());
    case FieldType::String: return static_cast<uint32_t>(vec<std::string>(l).size());
    default: return 0;
    }
}

Value listAt(const Value &l, uint32_t i) {
    if (l.kind != Value::Kind::List) { return Value(); }
    switch (static_cast<FieldType>(l.type)) {
    case FieldType::Bool: {
        const std::vector<bool> &v = vec<bool>(l);
        return (i < v.size()) ? Value::ofBool(v[i]) : Value();
    }
    case FieldType::I8: return elem<int8_t>(l, i, [](int8_t v) { return mkInt(v); });
    case FieldType::U8: return elem<uint8_t>(l, i, [](uint8_t v) { return mkUint(v); });
    case FieldType::I16: return elem<int16_t>(l, i, [](int16_t v) { return mkInt(v); });
    case FieldType::U16: return elem<uint16_t>(l, i, [](uint16_t v) { return mkUint(v); });
    case FieldType::I32: return elem<int32_t>(l, i, [](int32_t v) { return mkInt(v); });
    case FieldType::U32: return elem<uint32_t>(l, i, [](uint32_t v) { return mkUint(v); });
    case FieldType::I64: return elem<int64_t>(l, i, [](int64_t v) { return mkInt(v); });
    case FieldType::U64: return elem<uint64_t>(l, i, [](uint64_t v) { return mkUint(v); });
    case FieldType::F32: return elem<float>(l, i, [](float v) { return mkFloat(v); });
    case FieldType::F64: return elem<double>(l, i, [](double v) { return mkFloat(v); });
    case FieldType::String: {
        const std::vector<std::string> &v = vec<std::string>(l);
        return (i < v.size()) ? Value::ofString(&v[i]) : Value();
    }
    default: return Value();
    }
}

} // namespace reflect
} // namespace ast
} // namespace svcheck
