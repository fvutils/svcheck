/*
 * Syntax.h
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
 * The syntax schema: slang's syntax tree as the rule engine sees it
 * (svcheck-plan P1.2; svcheck-design §3.2).
 *
 * - The classes and fields are generated from slang's syntax.txt
 *   (scripts/gen_syntax_schema.py, which describes the layout): one class
 *   per syntax type (`ClassDeclarationSyntax`) and one per kind
 *   (`ClassDeclaration`), the kind deriving from its type.
 * - A token is a `Token` struct: `text` (slang's valueText: an identifier's
 *   name, a string literal's value), `raw` (the source text), `kind`
 *   (a `TokenKind`), `missing` (the parser made it up), `line`, `col`.
 * - A node's location is where its first token is, macro expansions mapped
 *   to where the macro is used (S-3); a named declaration's is its name's.
 */
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "Reflect.h"
#include "slang/text/SourceLocation.h"

namespace slang {
class SourceManager;
namespace syntax {
class SyntaxNode;
}
}

namespace svcheck {
namespace host {

namespace rf = svcheck::ast::reflect;

/**
 * The syntax schema's own tables, with its own ids (SyntaxNode is class 1),
 * and its location hooks. Schema.cpp installs them after the semantic
 * layer's and calls setSyntaxBases() with where they start.
 */
const rf::Schema &syntaxSchema();

/** Where the syntax tables start in the installed schema (Schema.cpp). */
void setSyntaxBases(uint16_t cls, uint16_t field, uint16_t enm, uint16_t st);

/** Whether `n` is a syntax node (its class is in the syntax tables). */
bool isSyntaxNode(const rf::Node &n);

/** The node for a syntax node; a null node for nullptr. */
rf::Node syntaxNode(const slang::syntax::SyntaxNode *n);

/** The macro a syntax node's first token comes from (Sources::macroOf); "" for none. */
std::string syntaxMacroOf(const rf::Node &n);

/**
 * The files of one run, and where things are in them. Files get ids in
 * the order they are first located (or added), from 0; the ids index
 * paths(), which is what the engine's Context takes (setFiles).
 */
class Sources {
public:
    explicit Sources(const slang::SourceManager &sm) : m_sm(sm) { }

    /** The id of the file a buffer belongs to, assigned on first use. */
    int32_t fileid(slang::BufferID buffer);

    /**
     * The same, naming the file `name` when this assigns its id: for a
     * buffer made from text, which has no path.
     */
    int32_t fileid(slang::BufferID buffer, const std::string &name);

    /** The path of the file whose text `loc` is in, macro expansions mapped to their use. */
    std::string pathOf(slang::SourceLocation loc);

    /**
     * The macro whose expansion `loc` is in, the outermost when expansions
     * nest; "" when it is in none. A macro argument is where it is written:
     * in the macro its use is in, if any.
     */
    std::string macroOf(slang::SourceLocation loc);

    const std::vector<std::string> &paths() const { return m_paths; }

    /**
     * Where `range` starts, as the source shows it: a location inside a
     * macro expansion is mapped to the macro's use. The extent is the
     * range's length when it ends on the same line, else 0.
     */
    bool locate(slang::SourceRange range, ast::Location &loc);

private:
    // What is known of a buffer, cached so that most queries, on a file's
    // own text, take no lock in the SourceManager (svcheck-plan P1.7).
    struct Buffer {
        enum Kind : uint8_t { Unknown, File, Macro } kind = Unknown;
        std::string_view    text;           // a File's
        bool                macroKnown = false;
        std::string         macro;          // a Macro's macroOf(): the same at every offset
    };
    Buffer &buffer(slang::SourceLocation loc);
    // getFullyExpandedLoc, with no lock for a location in a file's text.
    slang::SourceLocation expanded(slang::SourceLocation loc);

    const slang::SourceManager                  &m_sm;
    std::unordered_map<uint32_t, int32_t>       m_ids;
    std::vector<std::string>                    m_paths;
    std::unordered_map<uint32_t, Buffer>        m_buffers;  // by buffer id (sparse)
};

/**
 * The Sources the schema's location hooks read, for the run in progress;
 * null for none, when nothing is located.
 */
void setSources(Sources *s);

}
}
