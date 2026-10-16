/*
 * SlangSchema.cpp
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
 * The classes are slang's, by its C++ names, with the fields its accessors
 * give (`getBaseClass()` is `baseClass`). Two names are taken by syntax
 * kinds, so they have an `Ast` prefix: AstExpressionStatement,
 * AstNewClassExpression. A node's pointer is slang's object: a Symbol, a
 * Statement or an Expression, which its class id says.
 *
 * What is computed rather than read: `members` (a scope's), the children of
 * a statement or expression (slang's own visitExprs/visitStmts), and
 * `inLibrary`. As in the curated model, a library subroutine has no body.
 */
#include <deque>
#include <string>
#include <type_traits>
#include <vector>
#include "SlangSchema.h"
#include "Syntax.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/symbols/ClassSymbols.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/text/SourceManager.h"

namespace svcheck {
namespace host {

namespace {

namespace sa = slang::ast;

enum Cls : uint16_t {
    AstNode = 1, Symbol, PackageSymbol, ClassType, GenericClassDefSymbol, SubroutineSymbol,
    VariableSymbol, TypeAliasType,
    Statement, AstExpressionStatement, ImmediateAssertionStatement,
    Expression, CallExpression, AstNewClassExpression, ConversionExpression,
    NumCls = ConversionExpression
};

bool isSymbol(uint16_t k) { return k >= Symbol && k <= TypeAliasType; }
bool isStmt(uint16_t k) { return k >= Statement && k <= ImmediateAssertionStatement; }
bool isExpr(uint16_t k) { return k >= Expression && k <= ConversionExpression; }

Sources                     *s_sources = nullptr;
const slang::SourceManager  *s_sm = nullptr;
std::vector<std::string>    s_libraryDirs;
std::deque<std::string>     s_strings;      // what the string fields made, for the run

// One node's children at a time: a list field's size() and at() ask for
// the same node in turn. Reset with the run.
struct Cache {
    const void                              *kidsOf = nullptr;
    const void                              *membersOf = nullptr;
    std::vector<const slang::ast::Symbol *> members;
};

const sa::Symbol &sym(void *p) { return *static_cast<const sa::Symbol *>(p); }
const sa::Statement &stmt(void *p) { return *static_cast<const sa::Statement *>(p); }
const sa::Expression &expr(void *p) { return *static_cast<const sa::Expression *>(p); }

rf::Value str(std::string s) {
    s_strings.push_back(std::move(s));
    return rf::Value::ofString(std::string_view(s_strings.back()));
}

rf::Node symbolNode(const sa::Symbol *s) {
    rf::Node n;
    if (!s) {
        return n;
    }
    switch (s->kind) {
    case sa::SymbolKind::Package: n.kind = PackageSymbol; break;
    case sa::SymbolKind::ClassType: n.kind = ClassType; break;
    case sa::SymbolKind::GenericClassDef: n.kind = GenericClassDefSymbol; break;
    case sa::SymbolKind::Subroutine: n.kind = SubroutineSymbol; break;
    case sa::SymbolKind::Variable:
    case sa::SymbolKind::ClassProperty:
    case sa::SymbolKind::FormalArgument: n.kind = VariableSymbol; break;
    case sa::SymbolKind::TypeAlias: n.kind = TypeAliasType; break;
    default: n.kind = Symbol; break;
    }
    n.ptr = const_cast<sa::Symbol *>(s);
    return n;
}

rf::Node stmtNode(const sa::Statement *s) {
    rf::Node n;
    if (!s) {
        return n;
    }
    switch (s->kind) {
    case sa::StatementKind::ExpressionStatement: n.kind = AstExpressionStatement; break;
    case sa::StatementKind::ImmediateAssertion: n.kind = ImmediateAssertionStatement; break;
    default: n.kind = Statement; break;
    }
    n.ptr = const_cast<sa::Statement *>(s);
    return n;
}

rf::Node exprNode(const sa::Expression *e) {
    rf::Node n;
    if (!e) {
        return n;
    }
    switch (e->kind) {
    case sa::ExpressionKind::Call: n.kind = CallExpression; break;
    case sa::ExpressionKind::NewClass: n.kind = AstNewClassExpression; break;
    case sa::ExpressionKind::Conversion: n.kind = ConversionExpression; break;
    default: n.kind = Expression; break;
    }
    n.ptr = const_cast<sa::Expression *>(e);
    return n;
}

bool inLibrary(const sa::Symbol &s) {
    if (!s.location.valid()) {
        return true;        // built in: the std package
    }
    if (!s_sm) {
        return false;
    }
    slang::SourceLocation e = s_sm->getFullyExpandedLoc(s.location);
    std::string p = e.valid() ? s_sm->getFullPath(e.buffer()).string() : std::string();
    for (const std::string &d : s_libraryDirs) {
        if (p.compare(0, d.size(), d) == 0) {
            return true;
        }
    }
    return false;
}

// The direct children of a statement or expression: slang's visitExprs and
// visitStmts, which a local variable's initializer is not part of.
struct Kids {
    std::vector<const sa::Statement *>  stmts;
    std::vector<const sa::Expression *> exprs;
    template <class T> void visit(const T &t) {
        if constexpr (std::is_base_of_v<sa::Statement, T>) {
            stmts.push_back(&t);
        } else if constexpr (std::is_base_of_v<sa::Expression, T>) {
            exprs.push_back(&t);
        }
    }
};

struct KidsOf {
    Kids &out;
    template <class T> void visit(const T &t) {
        if constexpr (requires { t.visitExprs(out); }) {
            t.visitExprs(out);
        }
        if constexpr (requires { t.visitStmts(out); }) {
            t.visitStmts(out);
        }
    }
};

Cache s_cache;
Kids s_kids;

const Kids &kids(void *p, uint16_t k) {
    Kids &cached = s_kids;
    if (p != s_cache.kidsOf) {
        s_cache.kidsOf = p;
        cached = Kids();
        KidsOf v{cached};
        if (isStmt(k)) {
            const sa::Statement &s = stmt(p);
            s.visit(v);
            if (s.kind == sa::StatementKind::VariableDeclaration) {
                if (const sa::Expression *init =
                        s.as<sa::VariableDeclStatement>().symbol.getInitializer()) {
                    cached.exprs.push_back(init);
                }
            }
        } else if (isExpr(k)) {
            expr(p).visit(v);
        }
    }
    return cached;
}

// A scope's members, the same way.
const std::vector<const sa::Symbol *> &members(void *p) {
    std::vector<const sa::Symbol *> &cached = s_cache.members;
    if (p != s_cache.membersOf) {
        s_cache.membersOf = p;
        cached.clear();
        if (sym(p).isScope()) {
            for (const sa::Symbol &m : sym(p).as<sa::Scope>().members()) {
                cached.push_back(&m);
            }
        }
    }
    return cached;
}

//* Getters

rf::Value gInLibrary(void *p, uint16_t k) {
    return rf::Value::ofBool(isSymbol(k) && inLibrary(sym(p)));
}
rf::Value gName(void *p, uint16_t) { return rf::Value::ofString(sym(p).name); }
rf::Value gSymbolKind(void *p, uint16_t) { return rf::Value::ofString(sa::toString(sym(p).kind)); }
uint32_t sMembers(void *p, uint16_t) { return static_cast<uint32_t>(members(p).size()); }
rf::Value aMembers(void *p, uint16_t, uint32_t i) { return rf::Value::ofNode(symbolNode(members(p)[i])); }

rf::Value gIsAbstract(void *p, uint16_t) { return rf::Value::ofBool(sym(p).as<sa::ClassType>().isAbstract); }
rf::Value gBaseClass(void *p, uint16_t) {
    const sa::Type *b = sym(p).as<sa::ClassType>().getBaseClass();
    return rf::Value::ofNode((b && b->isClass()) ? symbolNode(&b->getCanonicalType()) : rf::Node());
}

rf::Value gIsVirtual(void *p, uint16_t) { return rf::Value::ofBool(sym(p).as<sa::SubroutineSymbol>().isVirtual()); }
rf::Value gOverride(void *p, uint16_t) {
    return rf::Value::ofNode(symbolNode(sym(p).as<sa::SubroutineSymbol>().getOverride()));
}
rf::Value gBody(void *p, uint16_t) {
    const auto &s = sym(p).as<sa::SubroutineSymbol>();
    if (inLibrary(s) || s.flags.has(sa::MethodFlags::Pure)) {
        return rf::Value::ofNode(rf::Node());
    }
    const sa::Statement &b = s.getBody();
    return rf::Value::ofNode(b.kind == sa::StatementKind::Invalid ? rf::Node() : stmtNode(&b));
}

rf::Value gVarTypeName(void *p, uint16_t) { return str(sym(p).as<sa::VariableSymbol>().getType().toString()); }
rf::Value gInitializer(void *p, uint16_t) {
    const auto &v = sym(p).as<sa::VariableSymbol>();
    return rf::Value::ofNode(inLibrary(v) ? rf::Node() : exprNode(v.getInitializer()));
}
rf::Value gTargetType(void *p, uint16_t) {
    return str(sym(p).as<sa::TypeAliasType>().targetType.getType().toString());
}

rf::Value gStatementKind(void *p, uint16_t) { return rf::Value::ofString(sa::toString(stmt(p).kind)); }
uint32_t sStmts(void *p, uint16_t k) { return static_cast<uint32_t>(kids(p, k).stmts.size()); }
rf::Value aStmts(void *p, uint16_t k, uint32_t i) { return rf::Value::ofNode(stmtNode(kids(p, k).stmts[i])); }
uint32_t sExprs(void *p, uint16_t k) { return static_cast<uint32_t>(kids(p, k).exprs.size()); }
rf::Value aExprs(void *p, uint16_t k, uint32_t i) { return rf::Value::ofNode(exprNode(kids(p, k).exprs[i])); }

rf::Value gExpressionKind(void *p, uint16_t) { return rf::Value::ofString(sa::toString(expr(p).kind)); }
rf::Value gType(void *p, uint16_t) {
    const sa::Type *t = expr(p).type;
    return rf::Value::ofNode((t && t->isClass()) ? symbolNode(&t->getCanonicalType()) : rf::Node());
}
rf::Value gTypeName(void *p, uint16_t) {
    const sa::Type *t = expr(p).type;
    return t ? str(t->toString()) : rf::Value();
}

rf::Value gSubroutineName(void *p, uint16_t) {
    return rf::Value::ofString(expr(p).as<sa::CallExpression>().getSubroutineName());
}
rf::Value gSubroutine(void *p, uint16_t) {
    const auto &c = expr(p).as<sa::CallExpression>();
    return rf::Value::ofNode(c.isSystemCall() ? rf::Node() : symbolNode(std::get<0>(c.subroutine)));
}
rf::Value gIsSystemCall(void *p, uint16_t) {
    return rf::Value::ofBool(expr(p).as<sa::CallExpression>().isSystemCall());
}

using FT = rf::FieldType;

// Fields, contiguous per class, in class order.
const rf::FieldInfo fields[] = {
    // AstNode
    {"inLibrary", AstNode, FT::Bool, FT::Bool, 0, false, &gInLibrary, nullptr, nullptr},
    // Symbol
    {"name", Symbol, FT::String, FT::Bool, 0, false, &gName, nullptr, nullptr},
    {"symbolKind", Symbol, FT::String, FT::Bool, 0, false, &gSymbolKind, nullptr, nullptr},
    {"members", Symbol, FT::List, FT::Node, Symbol, true, nullptr, &sMembers, &aMembers},
    // ClassType
    {"isAbstract", ClassType, FT::Bool, FT::Bool, 0, false, &gIsAbstract, nullptr, nullptr},
    {"baseClass", ClassType, FT::Node, FT::Bool, ClassType, false, &gBaseClass, nullptr, nullptr},
    // SubroutineSymbol
    {"isVirtual", SubroutineSymbol, FT::Bool, FT::Bool, 0, false, &gIsVirtual, nullptr, nullptr},
    {"override", SubroutineSymbol, FT::Node, FT::Bool, SubroutineSymbol, false, &gOverride, nullptr, nullptr},
    {"body", SubroutineSymbol, FT::Node, FT::Bool, Statement, true, &gBody, nullptr, nullptr},
    // VariableSymbol
    {"typeName", VariableSymbol, FT::String, FT::Bool, 0, false, &gVarTypeName, nullptr, nullptr},
    {"initializer", VariableSymbol, FT::Node, FT::Bool, Expression, true, &gInitializer, nullptr, nullptr},
    // TypeAliasType
    {"targetType", TypeAliasType, FT::String, FT::Bool, 0, false, &gTargetType, nullptr, nullptr},
    // Statement
    {"statementKind", Statement, FT::String, FT::Bool, 0, false, &gStatementKind, nullptr, nullptr},
    {"stmts", Statement, FT::List, FT::Node, Statement, true, nullptr, &sStmts, &aStmts},
    {"exprs", Statement, FT::List, FT::Node, Expression, true, nullptr, &sExprs, &aExprs},
    // Expression
    {"expressionKind", Expression, FT::String, FT::Bool, 0, false, &gExpressionKind, nullptr, nullptr},
    {"type", Expression, FT::Node, FT::Bool, ClassType, false, &gType, nullptr, nullptr},
    {"typeName", Expression, FT::String, FT::Bool, 0, false, &gTypeName, nullptr, nullptr},
    {"operands", Expression, FT::List, FT::Node, Expression, true, nullptr, &sExprs, &aExprs},
    // CallExpression
    {"subroutineName", CallExpression, FT::String, FT::Bool, 0, false, &gSubroutineName, nullptr, nullptr},
    {"subroutine", CallExpression, FT::Node, FT::Bool, SubroutineSymbol, false, &gSubroutine, nullptr, nullptr},
    {"isSystemCall", CallExpression, FT::Bool, FT::Bool, 0, false, &gIsSystemCall, nullptr, nullptr},
};

const rf::ClassInfo classes[NumCls + 1] = {
    {"", 0, 0, 0},
    {"AstNode", 0, 0, 1},
    {"Symbol", AstNode, 1, 3},
    {"PackageSymbol", Symbol, 4, 0},
    {"ClassType", Symbol, 4, 2},
    {"GenericClassDefSymbol", Symbol, 6, 0},
    {"SubroutineSymbol", Symbol, 6, 3},
    {"VariableSymbol", Symbol, 9, 2},
    {"TypeAliasType", Symbol, 11, 1},
    {"Statement", AstNode, 12, 3},
    {"AstExpressionStatement", Statement, 15, 0},
    {"ImmediateAssertionStatement", Statement, 15, 0},
    {"Expression", AstNode, 15, 4},
    {"CallExpression", Expression, 19, 3},
    {"AstNewClassExpression", Expression, 22, 0},
    {"ConversionExpression", Expression, 22, 0},
};

bool location(const rf::Node &n, ast::Location &loc) {
    loc = ast::Location();
    if (!n || !s_sources) {
        return false;
    }
    if (isSymbol(n.kind)) {
        const sa::Symbol &s = sym(n.ptr);
        return s.location.valid()
            && s_sources->locate(slang::SourceRange(s.location, s.location + s.name.size()), loc);
    }
    if (isStmt(n.kind)) {
        return s_sources->locate(stmt(n.ptr).sourceRange, loc);
    }
    return isExpr(n.kind) && s_sources->locate(expr(n.ptr).sourceRange, loc);
}

bool nameLocation(const rf::Node &n, ast::Location &loc) {
    loc = ast::Location();
    return isSymbol(n.kind) && location(n, loc);
}

}

const rf::Schema &slangSchema() {
    static const rf::Schema s = [] {
        rf::Schema r;
        r.numClasses = NumCls;
        r.classes = classes;
        r.fields = fields;
        r.numFields = static_cast<uint16_t>(std::size(fields));
        r.location = &location;
        r.nameLocation = &nameLocation;
        return r;
    }();
    return s;
}

rf::Node slangRoot(const slang::ast::Compilation &c) {
    return symbolNode(&const_cast<slang::ast::Compilation &>(c).getRoot());
}

void setSlangSources(Sources *s, const slang::SourceManager *sm,
        const std::vector<std::string> &libraryDirs) {
    s_sources = s;
    s_sm = sm;
    s_libraryDirs = libraryDirs;
    s_cache = Cache();
    s_kids = Kids();
    if (!s) {
        s_strings.clear();
    }
}

bool slangDerivesFrom(const rf::Node &n, const std::string &name) {
    if (n.kind != ClassType) {
        return false;
    }
    bool q = name.find("::") != std::string::npos;
    for (const sa::Type *t = &sym(n.ptr).as<sa::ClassType>(); t && t->isClass(); ) {
        const sa::ClassType &c = t->getCanonicalType().as<sa::ClassType>();
        if (q ? c.getLexicalPath() == name : c.name == name) {
            return true;
        }
        t = c.getBaseClass();
    }
    return false;
}

}
}
