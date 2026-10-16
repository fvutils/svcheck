/*
 * SyntaxValues.h
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
 * What the generated syntax tables (SyntaxSchema.cpp, from
 * scripts/gen_syntax_schema.py) and the hand-written half of the syntax
 * schema (Syntax.cpp) share.
 */
#pragma once
#include <cstdint>
#include "Reflect.h"
#include "slang/parsing/Token.h"
#include "slang/syntax/SyntaxKind.h"
#include "slang/syntax/SyntaxNode.h"

namespace svcheck {
namespace host {
namespace syntax {

namespace rf = svcheck::ast::reflect;

/**
 * The struct and enum indexes in the syntax schema's own tables (Syntax.cpp),
 * which the generated tables use. The installed schema puts the syntax
 * tables after the semantic layer's (Schema.cpp), so a value made at run
 * time adds that layer's counts (Bases).
 */
constexpr uint16_t TokenStruct = 0;
constexpr uint16_t TokenKindEnum = 0;

/** Where the syntax tables start in the installed schema. */
struct Bases {
    uint16_t    cls = 0;        // added to a class id
    uint16_t    field = 0;      // to a field index
    uint16_t    enm = 0;        // to an enum index
    uint16_t    st = 0;         // to a struct index
};
extern Bases bases;

/** The generated tables. */
extern const uint16_t NumClasses;
extern const rf::ClassInfo classes[];
extern const uint16_t numFields;
extern const rf::FieldInfo fields[];

/** The class of a node of kind `k`: the kind's own; SyntaxNode (1) for one not in syntax.txt. */
uint16_t classOf(slang::syntax::SyntaxKind k);

/** A node field's value: the node, or a null node. */
rf::Value nodeValue(const slang::syntax::SyntaxNode *n);

/** A token field's value: a Token struct, or absent for a token the source does not have. */
rf::Value tokenValue(const slang::parsing::Token &t);

}
}
}
