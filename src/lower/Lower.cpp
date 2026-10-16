/*
 * Lower.cpp
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
 * Three steps, so that order of declaration never matters:
 * 1. declarations: a node for every package, definition, class and member,
 *    recorded by its slang symbol;
 * 2. links: bases, supers, overrides, imports, types;
 * 3. bodies: user subroutines' statements, and initializers, which refer
 *    to the nodes the first two steps made.
 */
#include <algorithm>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Lower.h"

#include "Assign.h"
#include "Block.h"
#include "Call.h"
#include "Case.h"
#include "CaseItem.h"
#include "Class.h"
#include "Conversion.h"
#include "Definition.h"
#include "Design.h"
#include "ExprStmt.h"
#include "If.h"
#include "ImmediateAssert.h"
#include "Literal.h"
#include "Loop.h"
#include "MemberAccess.h"
#include "NamedValue.h"
#include "NewClass.h"
#include "OpaqueDecl.h"
#include "OpaqueExpr.h"
#include "OpaqueStmt.h"
#include "Package.h"
#include "Return.h"
#include "Subroutine.h"
#include "TypeRef.h"
#include "Typedef.h"
#include "VarDecl.h"
#include "Variable.h"

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/Expression.h"
#include "slang/ast/Statement.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/LoopStatements.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/symbols/AttributeSymbol.h"
#include "slang/ast/symbols/ClassSymbols.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/syntax/AllSyntax.h"

namespace svcheck {
namespace lower {

namespace sa = slang::ast;

namespace {

std::string trimmed(std::string s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    size_t e = s.find_last_not_of(" \t\r\n");
    return (b == std::string::npos) ? std::string() : s.substr(b, e - b + 1);
}

/** The direct child statements and expressions of a slang statement or expression. */
struct Children {
    std::vector<const sa::Statement *>  stmts;
    std::vector<const sa::Expression *> exprs;

    // Called through Statement::visit / Expression::visit for each child.
    template <class T> void visit(const T &t) {
        if constexpr (std::is_base_of_v<sa::Statement, T>) {
            stmts.push_back(&t);
        } else if constexpr (std::is_base_of_v<sa::Expression, T>) {
            exprs.push_back(&t);
        }
    }
};

// Calls the child-visiting methods of the node's own class, when it has them.
struct ChildDispatch {
    Children &out;
    template <class T> void visit(const T &t) {
        if constexpr (requires { t.visitExprs(out); }) {
            t.visitExprs(out);
        }
        if constexpr (requires { t.visitStmts(out); }) {
            t.visitStmts(out);
        }
    }
};

Children childrenOf(const sa::Statement &s) {
    Children c;
    ChildDispatch d{c};
    s.visit(d);
    return c;
}

Children childrenOf(const sa::Expression &e) {
    Children c;
    ChildDispatch d{c};
    e.visit(d);
    return c;
}

class Lowerer {
public:
    Lowerer(sa::Compilation &c, const LowerHost &host) : m_comp(c), m_host(host) { }

    std::unique_ptr<sem::IDesign> run() {
        sem::Design *d = new sem::Design();
        std::unique_ptr<sem::IDesign> design(d);
        m_design = d;
        const sa::RootSymbol &root = m_comp.getRoot();

        // 1. Declarations.
        for (const sa::PackageSymbol *p : m_comp.getPackages()) {
            declPackage(*p);
        }
        for (const sa::Symbol *s : m_comp.getDefinitions()) {
            if (s->kind == sa::SymbolKind::Definition) {
                declDefinition(s->as<sa::DefinitionSymbol>());
            }
        }
        for (const sa::InstanceSymbol *inst : root.topInstances) {
            declInstance(*inst);
        }
        for (const sa::CompilationUnitSymbol *u : root.compilationUnits) {
            for (const sa::Symbol &m : u->members()) {
                if (sem::IDecl *n = declMember(m, nullptr, "")) {
                    d->getItems().push_back(sem::IDeclUP(n));
                }
            }
        }

        // 2. Links.
        links();

        // 3. Bodies, in declaration order.
        for (auto &[sym, sub] : m_bodies) {
            const sa::Statement &body = sym->getBody();
            if (body.kind != sa::StatementKind::Invalid) {
                sub->setBody(lowerStmt(body));
            }
        }
        for (auto &[sym, var] : m_inits) {
            if (const sa::Expression *init = sym->getInitializer()) {
                var->setInit(lowerExpr(*init));
            }
        }
        return design;
    }

private:
    //* Declarations.

    void setLocation(sem::ISemNode *n, slang::SourceRange r) {
        sem::Location loc;
        if (m_host.locate && m_host.locate(r, loc)) {
            n->setLocation(loc);
        }
        if (m_host.macroOf && r.start().valid()) {
            n->setMacro(m_host.macroOf(r.start()));
        }
    }

    void setDecl(sem::IDecl *n, const sa::Symbol &s, sem::IDecl *carrier, const std::string &prefix) {
        n->setName(std::string(s.name));
        n->setQname(prefix.empty() ? std::string(s.name) : prefix + "::" + std::string(s.name));
        n->setInLibrary(library(s));
        n->setCarrier(carrier);
        if (s.kind == sa::SymbolKind::Package || s.kind == sa::SymbolKind::Definition
                || s.kind == sa::SymbolKind::ClassType
                || s.kind == sa::SymbolKind::GenericClassDef) {
            n->getTags() = tagsOf(s);
            // The attribute form (Q-CX1): (* svcheck_context = "agent(apb)" *).
            for (const sa::AttributeSymbol *a : m_comp.getAttributes(s)) {
                if (a->name == "svcheck_context") {
                    const slang::ConstantValue &v = a->getValue();
                    std::string tag = trimmed(v.isString() ? v.str() : v.convertToStr().str());
                    if (!tag.empty()) {
                        n->getTags().push_back(tag);
                    }
                }
            }
        }
        if (s.location.valid()) {
            setLocation(n, slang::SourceRange(s.location, s.location + s.name.size()));
        }
        m_decls[&s] = n;
    }

    // The context tags in the comments before a declaration: each item of
    // `// svcheck context: agent(apb), hdl` (sv-contexts.md §3.1, Q-CX1).
    static std::vector<std::string> tagsOf(const sa::Symbol &s) {
        std::vector<std::string> out;
        const slang::syntax::SyntaxNode *syn = s.getSyntax();
        if (!syn) {
            return out;
        }
        static const std::string key = "svcheck context:";
        for (const slang::parsing::Trivia &t : syn->getFirstToken().trivia()) {
            if (t.kind != slang::parsing::TriviaKind::LineComment
                    && t.kind != slang::parsing::TriviaKind::BlockComment) {
                continue;
            }
            std::string text(t.getRawText());
            size_t p = text.find(key);
            if (p == std::string::npos) {
                continue;
            }
            text = text.substr(p + key.size());
            if (t.kind == slang::parsing::TriviaKind::BlockComment && text.size() >= 2
                    && text.compare(text.size() - 2, 2, "*/") == 0) {
                text.resize(text.size() - 2);
            }
            std::string item;
            int depth = 0;
            for (size_t i=0; i<=text.size(); i++) {
                char c = (i < text.size()) ? text[i] : ',';
                depth += (c == '(') - (c == ')');
                if (c == ',' && depth <= 0) {
                    std::string tag = trimmed(item);
                    if (!tag.empty()) {
                        out.push_back(tag);
                    }
                    item.clear();
                } else {
                    item += c;
                }
            }
        }
        return out;
    }

    bool library(const sa::Symbol &s) const {
        return m_host.isLibrary && m_host.isLibrary(s);
    }

    void addFile(std::vector<std::string> &files, slang::SourceLocation loc) {
        if (!loc.valid() || !m_host.filePath) {
            return;
        }
        std::string p = m_host.filePath(loc);
        if (!p.empty() && std::find(files.begin(), files.end(), p) == files.end()) {
            files.push_back(p);
        }
    }

    void declPackage(const sa::PackageSymbol &p) {
        sem::Package *n = new sem::Package();
        m_design->getPackages().push_back(sem::IPackageUP(n));
        setDecl(n, p, nullptr, "");
        m_packages[&p] = n;
        addFile(n->getFiles(), p.location);
        for (const sa::Symbol &m : p.members()) {
            addFile(n->getFiles(), m.location);
            if (sem::IDecl *d = declMember(m, n, std::string(p.name))) {
                n->getItems().push_back(sem::IDeclUP(d));
            }
        }
        m_imports.emplace_back(&p, n);
    }

    void declDefinition(const sa::DefinitionSymbol &def) {
        if (m_definitions.count(&def)) {
            return;
        }
        sem::Definition *n = new sem::Definition();
        m_design->getDefinitions().push_back(sem::IDefinitionUP(n));
        setDecl(n, def, nullptr, "");
        switch (def.definitionKind) {
        case sa::DefinitionKind::Module: n->setKind(sem::DefinitionKind::Module); break;
        case sa::DefinitionKind::Interface: n->setKind(sem::DefinitionKind::Interface); break;
        case sa::DefinitionKind::Program: n->setKind(sem::DefinitionKind::Program); break;
        }
        addFile(n->getFiles(), def.location);
        m_definitions[&def] = n;
    }

    // A definition's items come from its first instance body; each body
    // below it is visited for the definitions it instantiates.
    void declInstance(const sa::InstanceSymbol &inst) {
        const sa::DefinitionSymbol &def = inst.getDefinition();
        declDefinition(def);
        sem::Definition *n = m_definitions[&def];
        if (!m_bodiesSeen.insert(&def).second) {
            return;
        }
        for (const sa::Symbol &m : inst.body.members()) {
            if (m.kind == sa::SymbolKind::Instance) {
                declInstance(m.as<sa::InstanceSymbol>());
                continue;
            }
            addFile(n->getFiles(), m.location);
            if (sem::IDecl *d = declMember(m, n, std::string(def.name))) {
                n->getItems().push_back(sem::IDeclUP(d));
            }
        }
        m_imports.emplace_back(&inst.body, n);
    }

    // The node for a member of a package, definition, class or $unit; null
    // for one that is not a declaration of its own (imports, transparent
    // members, parameters of a class, packages and definitions).
    sem::IDecl *declMember(const sa::Symbol &m, sem::IDecl *carrier, const std::string &prefix) {
        switch (m.kind) {
        case sa::SymbolKind::ClassType:
            return declClass(m.as<sa::ClassType>(), &m, carrier, prefix);
        case sa::SymbolKind::GenericClassDef:
            return declGeneric(m.as<sa::GenericClassDefSymbol>(), carrier, prefix);
        case sa::SymbolKind::Subroutine:
            // slang's built-in class methods (randomize, srandom, ...) are not declarations.
            if (m.as<sa::SubroutineSymbol>().flags.has(sa::MethodFlags::BuiltIn)) {
                return nullptr;
            }
            return declSubroutine(m.as<sa::SubroutineSymbol>(), carrier, prefix);
        case sa::SymbolKind::MethodPrototype: {
            const sa::SubroutineSymbol *sub = m.as<sa::MethodPrototypeSymbol>().getSubroutine();
            if (sub && !m_subs.count(sub)) {
                return declSubroutine(*sub, carrier, prefix);
            }
            return nullptr;
        }
        case sa::SymbolKind::Variable:
        case sa::SymbolKind::ClassProperty:
            return declVariable(m.as<sa::VariableSymbol>(), carrier, prefix);
        case sa::SymbolKind::TypeAlias: {
            sem::Typedef *n = new sem::Typedef();
            setDecl(n, m, carrier, prefix);
            const sa::Type &t = m.as<sa::TypeAliasType>().targetType.getType();
            m_typedefs.emplace_back(&t, n);
            return n;
        }
        case sa::SymbolKind::WildcardImport:
        case sa::SymbolKind::ExplicitImport:
        case sa::SymbolKind::TransparentMember:
        case sa::SymbolKind::EnumValue:
        case sa::SymbolKind::Package:
        case sa::SymbolKind::Definition:
        case sa::SymbolKind::ForwardingTypedef:
        case sa::SymbolKind::EmptyMember:
            return nullptr;
        case sa::SymbolKind::Parameter:
        case sa::SymbolKind::TypeParameter:
            // A class's parameters are its `params`.
            if (m.getParentScope() && m.getParentScope()->asSymbol().kind == sa::SymbolKind::ClassType) {
                return nullptr;
            }
            break;
        default:
            break;
        }
        if (m.name.empty()) {
            return nullptr;
        }
        sem::OpaqueDecl *n = new sem::OpaqueDecl();
        setDecl(n, m, carrier, prefix);
        n->setSlangKind(std::string(sa::toString(m.kind)));
        return n;
    }

    sem::Class *newClass(const sa::Symbol &decl, sem::IDecl *carrier, const std::string &prefix) {
        sem::Class *n = new sem::Class();
        setDecl(n, decl, carrier, prefix);
        return n;
    }

    // A class's members, from the ClassType that has its body.
    void declClassBody(sem::Class *n, const sa::ClassType &ct, sem::IDecl *carrier) {
        n->setIsAbstract(ct.isAbstract);
        n->setIsInterface(ct.isInterface);
        m_classes.emplace_back(&ct, n);
        m_classOf[&ct] = n;
        for (const sa::Symbol &m : ct.members()) {
            if (sem::IDecl *d = declMember(m, carrier, n->getQname())) {
                n->getMembers().push_back(sem::IDeclUP(d));
            }
        }
    }

    sem::IDecl *declClass(const sa::ClassType &ct, const sa::Symbol *decl, sem::IDecl *carrier,
            const std::string &prefix) {
        sem::Class *n = newClass(*decl, carrier, prefix);
        declClassBody(n, ct, carrier);
        return n;
    }

    sem::IDecl *declGeneric(const sa::GenericClassDefSymbol &g, sem::IDecl *carrier,
            const std::string &prefix) {
        sem::Class *n = newClass(g, carrier, prefix);
        n->setIsInterface(g.isInterface);
        m_generics[&g] = n;
        const sa::Type *spec = nullptr;
        if (const sa::Scope *scope = g.getParentScope()) {
            spec = g.getDefaultSpecialization(*scope);
        }
        if (!spec || !spec->isClass()) {
            for (const sa::Type &t : g.specializations()) {
                spec = &t;
                break;
            }
        }
        if (spec && spec->isClass()) {
            const sa::ClassType &ct = spec->getCanonicalType().as<sa::ClassType>();
            // The parameters, from the specialisation's port parameters.
            for (const sa::Symbol &m : ct.members()) {
                if ((m.kind == sa::SymbolKind::Parameter
                        && m.as<sa::ParameterSymbol>().isPortParam())
                        || (m.kind == sa::SymbolKind::TypeParameter
                        && m.as<sa::TypeParameterSymbol>().isPortParam())) {
                    n->getParams().push_back(std::string(m.name));
                }
            }
            declClassBody(n, ct, carrier);
        }
        return n;
    }

    sem::IDecl *declSubroutine(const sa::SubroutineSymbol &s, sem::IDecl *carrier,
            const std::string &prefix) {
        sem::Subroutine *n = new sem::Subroutine();
        setDecl(n, s, carrier, prefix);
        n->setKind(s.subroutineKind == sa::SubroutineKind::Task
            ? sem::SubroutineKind::Task : sem::SubroutineKind::Function);
        n->setIsVirtual(s.isVirtual());
        n->setIsPure(s.flags.has(sa::MethodFlags::Pure));
        n->setIsStatic(s.flags.has(sa::MethodFlags::Static));
        m_returns.emplace_back(&s.getReturnType(), n);
        for (const sa::FormalArgumentSymbol *a : s.getArguments()) {
            n->getArgs().push_back(sem::IVariableUP(declVariable(*a, carrier, n->getQname())));
        }
        m_subs[&s] = n;
        if (!library(s) && !s.flags.has(sa::MethodFlags::Pure)) {
            m_bodies.emplace_back(&s, n);
        }
        return n;
    }

    sem::Variable *declVariable(const sa::VariableSymbol &v, sem::IDecl *carrier,
            const std::string &prefix) {
        sem::Variable *n = new sem::Variable();
        setDecl(n, v, carrier, prefix);
        const sa::Type &t = v.getType();
        n->setIsConst(v.flags.has(sa::VariableFlags::Const));
        n->setIsStatic(v.lifetime == sa::VariableLifetime::Static
            && v.kind == sa::SymbolKind::ClassProperty);
        if (v.kind == sa::SymbolKind::ClassProperty) {
            n->setIsRand(v.as<sa::ClassPropertySymbol>().randMode != sa::RandMode::None);
        }
        if (m_linked) {
            // A local, declared while bodies are lowered; its caller lowers the initializer.
            linkVariable(t, n);
        } else {
            m_vars.emplace_back(&t, n);
            if (!library(v)) {
                m_inits.emplace_back(&v, n);
            }
        }
        return n;
    }

    //* Links.

    void links() {
        for (auto &[ct, cls] : m_classes) {
            const sa::Type *base = ct->getBaseClass();
            if (base && base->isClass()) {
                cls->setBase(classNode(*base));
            }
        }
        for (auto &[sym, sub] : m_subs) {
            if (const sa::SubroutineSymbol *o = sym->getOverride()) {
                sub->setOverrides(subroutineNode(*o));
            }
        }
        for (auto &[sym, cls] : m_classes) {
            std::unordered_set<sem::IClass *> seen{cls};
            for (sem::IClass *b = cls->getBase(); b && seen.insert(b).second; b = b->getBase()) {
                cls->getSupers().push_back(b);
            }
        }
        for (auto &[scope, n] : m_imports) {
            std::vector<sem::IPackage *> &imps = importsOf(n);
            for (const sa::Symbol &m : scope->members()) {
                const sa::PackageSymbol *p = nullptr;
                if (m.kind == sa::SymbolKind::WildcardImport) {
                    p = m.as<sa::WildcardImportSymbol>().getPackage();
                } else if (m.kind == sa::SymbolKind::ExplicitImport) {
                    p = m.as<sa::ExplicitImportSymbol>().package();
                }
                auto it = p ? m_packages.find(p) : m_packages.end();
                if (it != m_packages.end()
                        && std::find(imps.begin(), imps.end(), it->second) == imps.end()) {
                    imps.push_back(it->second);
                }
            }
        }
        for (auto &[t, n] : m_typedefs) {
            n->setType(typeRef(*t));
        }
        for (auto &[t, n] : m_returns) {
            n->setReturnType(typeRef(*t));
        }
        for (auto &[t, n] : m_vars) {
            linkVariable(*t, n);
        }
        m_linked = true;
    }

    void linkVariable(const sa::Type &t, sem::Variable *n) {
        n->setType(typeRef(t));
    }

    // The TypeRef of a type, made once (Design.types). Called once the
    // declarations are made: it refers to their nodes.
    sem::ITypeRef *typeRef(const sa::Type &t) {
        auto it = m_types.find(&t);
        if (it != m_types.end()) {
            return it->second;
        }
        sem::TypeRef *n = new sem::TypeRef();
        m_design->getTypes().push_back(sem::ITypeRefUP(n));
        m_types[&t] = n;
        n->setName(t.toString());
        const sa::Type &c = t.getCanonicalType();
        n->setIsIntegral(c.isIntegral());
        n->setIsFourState(c.isIntegral() && c.isFourState());
        n->setIsString(c.isString());
        n->setBitWidth(c.isIntegral() ? static_cast<int32_t>(c.getBitWidth()) : 0);
        if (c.kind == sa::SymbolKind::ClassType) {
            n->setIsClass(true);
            n->setClassDecl(classNode(c));
            const sa::ClassType &ct = c.as<sa::ClassType>();
            if (ct.genericClass) {
                for (const sa::Symbol &m : ct.members()) {
                    if (m.kind == sa::SymbolKind::TypeParameter
                            && m.as<sa::TypeParameterSymbol>().isPortParam()) {
                        n->getArgs().push_back(
                            typeRef(m.as<sa::TypeParameterSymbol>().targetType.getType()));
                    }
                }
            }
        } else if (c.kind == sa::SymbolKind::VirtualInterfaceType) {
            n->setIsVirtualInterface(true);
            const sa::DefinitionSymbol &def = c.as<sa::VirtualInterfaceType>().iface.getDefinition();
            auto d = m_definitions.find(&def);
            if (d != m_definitions.end()) {
                n->setIfaceDecl(d->second);
            }
        }
        return n;
    }

    std::vector<sem::IPackage *> &importsOf(sem::IDecl *n) {
        if (auto *p = dynamic_cast<sem::IPackage *>(n)) {
            return p->getImports();
        }
        return dynamic_cast<sem::IDefinition *>(n)->getImports();
    }

    // The node for a class type: its generic's for a specialisation.
    sem::IClass *classNode(const sa::Type &t) {
        const sa::Type &c = t.getCanonicalType();
        if (c.kind != sa::SymbolKind::ClassType) {
            return nullptr;
        }
        const sa::ClassType &ct = c.as<sa::ClassType>();
        if (ct.genericClass) {
            auto it = m_generics.find(ct.genericClass);
            return (it == m_generics.end()) ? nullptr : it->second;
        }
        auto it = m_classOf.find(&ct);
        return (it == m_classOf.end()) ? nullptr : it->second;
    }

    // The node for a subroutine: by symbol, or, for a member of a
    // specialisation the generic's members were not lowered from, by name.
    sem::ISubroutine *subroutineNode(const sa::SubroutineSymbol &s) {
        auto it = m_subs.find(&s);
        if (it != m_subs.end()) {
            return it->second;
        }
        const sa::Scope *scope = s.getParentScope();
        if (!scope || scope->asSymbol().kind != sa::SymbolKind::ClassType) {
            return nullptr;
        }
        sem::IClass *cls = classNode(scope->asSymbol().as<sa::ClassType>());
        if (!cls) {
            return nullptr;
        }
        for (const sem::IDeclUP &m : cls->getMembers()) {
            if (m->getName() == s.name) {
                return dynamic_cast<sem::ISubroutine *>(m.get());
            }
        }
        return nullptr;
    }

    sem::IDecl *declNode(const sa::Symbol &s) {
        auto it = m_decls.find(&s);
        if (it != m_decls.end()) {
            return it->second;
        }
        // A member of a specialisation: the generic's member of that name.
        const sa::Scope *scope = s.getParentScope();
        if (scope && scope->asSymbol().kind == sa::SymbolKind::ClassType) {
            if (sem::IClass *cls = classNode(scope->asSymbol().as<sa::ClassType>())) {
                for (const sem::IDeclUP &m : cls->getMembers()) {
                    if (m->getName() == s.name) {
                        return m.get();
                    }
                }
            }
        }
        return nullptr;
    }

    //* Bodies.

    template <class T> T *located(T *n, slang::SourceRange r) {
        setLocation(n, r);
        return n;
    }

    sem::IStmt *lowerStmt(const sa::Statement &s) {
        switch (s.kind) {
        case sa::StatementKind::List: {
            sem::Block *b = located(new sem::Block(), s.sourceRange);
            for (const sa::Statement *c : s.as<sa::StatementList>().list) {
                b->getStmts().push_back(sem::IStmtUP(lowerStmt(*c)));
            }
            return b;
        }
        case sa::StatementKind::Block: {
            sem::Block *b = located(new sem::Block(), s.sourceRange);
            const sa::Statement &body = s.as<sa::BlockStatement>().body;
            if (body.kind == sa::StatementKind::List) {
                for (const sa::Statement *c : body.as<sa::StatementList>().list) {
                    b->getStmts().push_back(sem::IStmtUP(lowerStmt(*c)));
                }
            } else if (body.kind != sa::StatementKind::Empty) {
                b->getStmts().push_back(sem::IStmtUP(lowerStmt(body)));
            }
            return b;
        }
        case sa::StatementKind::ExpressionStatement: {
            sem::ExprStmt *n = located(new sem::ExprStmt(), s.sourceRange);
            n->setExpr(lowerExpr(s.as<sa::ExpressionStatement>().expr));
            return n;
        }
        case sa::StatementKind::VariableDeclaration: {
            sem::VarDecl *n = located(new sem::VarDecl(), s.sourceRange);
            const sa::VariableSymbol &v = s.as<sa::VariableDeclStatement>().symbol;
            sem::Variable *var = declVariable(v, nullptr, "");
            if (const sa::Expression *init = v.getInitializer()) {
                var->setInit(lowerExpr(*init));
            }
            n->setVar(var);
            return n;
        }
        case sa::StatementKind::Conditional: {
            const auto &c = s.as<sa::ConditionalStatement>();
            sem::If *n = located(new sem::If(), s.sourceRange);
            if (!c.conditions.empty()) {
                n->setCond(lowerExpr(*c.conditions[0].expr));
            }
            n->setIfTrue(lowerStmt(c.ifTrue));
            if (c.ifFalse) {
                n->setIfFalse(lowerStmt(*c.ifFalse));
            }
            return n;
        }
        case sa::StatementKind::Case: {
            const auto &c = s.as<sa::CaseStatement>();
            sem::Case *n = located(new sem::Case(), s.sourceRange);
            switch (c.check) {
            case sa::UniquePriorityCheck::None: n->setCheck(sem::CaseCheck::NoCheck); break;
            case sa::UniquePriorityCheck::Unique: n->setCheck(sem::CaseCheck::Unique); break;
            case sa::UniquePriorityCheck::Unique0: n->setCheck(sem::CaseCheck::Unique0); break;
            case sa::UniquePriorityCheck::Priority: n->setCheck(sem::CaseCheck::Priority); break;
            }
            switch (c.condition) {
            case sa::CaseStatementCondition::Normal: n->setCondition(sem::CaseCondition::Normal); break;
            case sa::CaseStatementCondition::WildcardXOrZ: n->setCondition(sem::CaseCondition::WildcardXOrZ); break;
            case sa::CaseStatementCondition::WildcardJustZ: n->setCondition(sem::CaseCondition::WildcardJustZ); break;
            case sa::CaseStatementCondition::Inside: n->setCondition(sem::CaseCondition::Inside); break;
            }
            n->setExpr(lowerExpr(c.expr));
            for (const auto &g : c.items) {
                sem::CaseItem *item = located(new sem::CaseItem(), g.stmt->sourceRange);
                for (const sa::Expression *e : g.expressions) {
                    item->getExprs().push_back(sem::IExprUP(lowerExpr(*e)));
                }
                item->setBody(lowerStmt(*g.stmt));
                n->getItems().push_back(sem::ICaseItemUP(item));
            }
            if (c.defaultCase) {
                n->setDefaultCase(lowerStmt(*c.defaultCase));
            }
            return n;
        }
        case sa::StatementKind::ForLoop:
        case sa::StatementKind::RepeatLoop:
        case sa::StatementKind::ForeachLoop:
        case sa::StatementKind::WhileLoop:
        case sa::StatementKind::DoWhileLoop:
        case sa::StatementKind::ForeverLoop: {
            sem::Loop *n = located(new sem::Loop(), s.sourceRange);
            n->setSlangKind(std::string(sa::toString(s.kind)));
            Children c = childrenOf(s);
            for (const sa::Expression *e : c.exprs) {
                n->getExprs().push_back(sem::IExprUP(lowerExpr(*e)));
            }
            if (!c.stmts.empty()) {
                n->setBody(lowerStmt(*c.stmts.front()));
            }
            return n;
        }
        case sa::StatementKind::Return: {
            sem::Return *n = located(new sem::Return(), s.sourceRange);
            if (const sa::Expression *e = s.as<sa::ReturnStatement>().expr) {
                n->setExpr(lowerExpr(*e));
            }
            return n;
        }
        case sa::StatementKind::ImmediateAssertion: {
            const auto &a = s.as<sa::ImmediateAssertionStatement>();
            sem::ImmediateAssert *n = located(new sem::ImmediateAssert(), s.sourceRange);
            switch (a.assertionKind) {
            case sa::AssertionKind::Assume: n->setKind(sem::AssertKind::Assume); break;
            case sa::AssertionKind::CoverProperty:
            case sa::AssertionKind::CoverSequence: n->setKind(sem::AssertKind::Cover); break;
            default: n->setKind(sem::AssertKind::Assert); break;
            }
            n->setCond(lowerExpr(a.cond));
            if (a.ifTrue) {
                n->setPass(lowerStmt(*a.ifTrue));
            }
            if (a.ifFalse) {
                n->setFail(lowerStmt(*a.ifFalse));
            }
            return n;
        }
        default: {
            sem::OpaqueStmt *n = located(new sem::OpaqueStmt(), s.sourceRange);
            n->setSlangKind(std::string(sa::toString(s.kind)));
            Children c = childrenOf(s);
            for (const sa::Statement *cs : c.stmts) {
                n->getStmts().push_back(sem::IStmtUP(lowerStmt(*cs)));
            }
            for (const sa::Expression *e : c.exprs) {
                n->getExprs().push_back(sem::IExprUP(lowerExpr(*e)));
            }
            return n;
        }
        }
    }

    template <class T> T *typed(T *n, const sa::Expression &e) {
        setLocation(n, e.sourceRange);
        if (e.type) {
            n->setType(typeRef(*e.type));
        }
        setConst(n, e);
        return n;
    }

    // Whether `e` is a constant the model keeps: a literal, a parameter or
    // enum value, or an operator over those. Calls are left out, so nothing
    // is run that could take long or fail.
    static bool constShape(const sa::Expression &e) {
        switch (e.kind) {
        case sa::ExpressionKind::IntegerLiteral:
        case sa::ExpressionKind::UnbasedUnsizedIntegerLiteral:
        case sa::ExpressionKind::StringLiteral:
            return true;
        case sa::ExpressionKind::NamedValue: {
            sa::SymbolKind k = e.as<sa::NamedValueExpression>().symbol.kind;
            return k == sa::SymbolKind::Parameter || k == sa::SymbolKind::EnumValue;
        }
        case sa::ExpressionKind::UnaryOp:
        case sa::ExpressionKind::BinaryOp:
        case sa::ExpressionKind::ConditionalOp:
        case sa::ExpressionKind::Concatenation:
        case sa::ExpressionKind::Replication:
        case sa::ExpressionKind::Conversion: {
            Children c = childrenOf(e);
            if (c.exprs.empty()) {
                return false;
            }
            for (const sa::Expression *x : c.exprs) {
                if (!constShape(*x)) {
                    return false;
                }
            }
            return true;
        }
        default:
            return false;
        }
    }

    void setConst(sem::IExpr *n, const sa::Expression &e) {
        if (!constShape(e)) {
            return;
        }
        if (e.kind == sa::ExpressionKind::StringLiteral) {
            // A string literal is a bit vector in SV; the model keeps its text.
            n->setConstKind(sem::ConstKind::String);
            n->setConstString(std::string(e.as<sa::StringLiteral>().getValue()));
            return;
        }
        sa::ASTContext actx(m_comp.getRoot(), sa::LookupLocation::max);
        sa::EvalContext ctx(actx);
        slang::ConstantValue v = e.eval(ctx);
        if (v.isInteger()) {
            const slang::SVInt &i = v.integer();
            std::optional<int64_t> x = i.hasUnknown() ? std::nullopt : i.as<int64_t>();
            if (x) {
                n->setConstKind(sem::ConstKind::Int);
                n->setConstInt(*x);
            }
        } else if (v.isString()) {
            n->setConstKind(sem::ConstKind::String);
            n->setConstString(v.str());
        }
    }

    sem::IExpr *lowerExpr(const sa::Expression &e) {
        switch (e.kind) {
        case sa::ExpressionKind::Call: {
            const auto &c = e.as<sa::CallExpression>();
            sem::Call *n = typed(new sem::Call(), e);
            n->setName(std::string(c.getSubroutineName()));
            n->setIsSystem(c.isSystemCall());
            if (!c.isSystemCall()) {
                const sa::SubroutineSymbol &sub = *std::get<0>(c.subroutine);
                n->setSubroutine(subroutineNode(sub));
                const sa::Scope *scope = sub.getParentScope();
                if (scope && scope->asSymbol().kind == sa::SymbolKind::ClassType) {
                    n->setClassType(typeRef(scope->asSymbol().as<sa::ClassType>()));
                }
            }
            n->setIsSuper(e.syntax && trimmed(e.syntax->toString()).rfind("super", 0) == 0);
            if (c.thisClass()) {
                n->setReceiver(lowerExpr(*c.thisClass()));
            }
            for (const sa::Expression *a : c.arguments()) {
                n->getArgs().push_back(sem::IExprUP(lowerExpr(*a)));
            }
            return n;
        }
        case sa::ExpressionKind::NewClass: {
            const auto &nc = e.as<sa::NewClassExpression>();
            sem::NewClass *n = typed(new sem::NewClass(), e);
            const sa::Expression *ctor = nc.constructorCall();
            if (ctor && ctor->kind == sa::ExpressionKind::Call) {
                for (const sa::Expression *a : ctor->as<sa::CallExpression>().arguments()) {
                    n->getArgs().push_back(sem::IExprUP(lowerExpr(*a)));
                }
            }
            return n;
        }
        case sa::ExpressionKind::Assignment: {
            const auto &a = e.as<sa::AssignmentExpression>();
            sem::Assign *n = typed(new sem::Assign(), e);
            n->setLhs(lowerExpr(a.left()));
            n->setRhs(lowerExpr(a.right()));
            n->setIsNonBlocking(a.isNonBlocking());
            return n;
        }
        case sa::ExpressionKind::NamedValue: {
            const auto &v = e.as<sa::NamedValueExpression>();
            sem::NamedValue *n = typed(new sem::NamedValue(), e);
            n->setName(std::string(v.symbol.name));
            n->setDecl(declNode(v.symbol));
            return n;
        }
        case sa::ExpressionKind::MemberAccess: {
            const auto &m = e.as<sa::MemberAccessExpression>();
            sem::MemberAccess *n = typed(new sem::MemberAccess(), e);
            n->setValue(lowerExpr(m.value()));
            n->setMember(std::string(m.member.name));
            n->setDecl(declNode(m.member));
            return n;
        }
        case sa::ExpressionKind::IntegerLiteral:
        case sa::ExpressionKind::RealLiteral:
        case sa::ExpressionKind::TimeLiteral:
        case sa::ExpressionKind::UnbasedUnsizedIntegerLiteral:
        case sa::ExpressionKind::NullLiteral:
        case sa::ExpressionKind::UnboundedLiteral:
        case sa::ExpressionKind::StringLiteral: {
            sem::Literal *n = typed(new sem::Literal(), e);
            if (e.syntax) {
                n->setText(trimmed(e.syntax->toString()));
            }
            return n;
        }
        case sa::ExpressionKind::Conversion: {
            const auto &c = e.as<sa::ConversionExpression>();
            sem::Conversion *n = typed(new sem::Conversion(), e);
            n->setOperand(lowerExpr(c.operand()));
            n->setIsImplicit(c.isImplicit());
            return n;
        }
        default: {
            sem::OpaqueExpr *n = typed(new sem::OpaqueExpr(), e);
            n->setSlangKind(std::string(sa::toString(e.kind)));
            for (const sa::Expression *c : childrenOf(e).exprs) {
                n->getOperands().push_back(sem::IExprUP(lowerExpr(*c)));
            }
            return n;
        }
        }
    }

    sa::Compilation                                                 &m_comp;
    const LowerHost                                                 &m_host;
    sem::Design                                                     *m_design = nullptr;
    bool                                                            m_linked = false;

    std::unordered_map<const sa::Symbol *, sem::IDecl *>            m_decls;
    std::unordered_map<const sa::PackageSymbol *, sem::IPackage *>  m_packages;
    std::unordered_map<const sa::DefinitionSymbol *, sem::Definition *>  m_definitions;
    std::unordered_set<const sa::DefinitionSymbol *>                m_bodiesSeen;
    std::unordered_map<const sa::GenericClassDefSymbol *, sem::Class *>  m_generics;
    std::unordered_map<const sa::ClassType *, sem::Class *>         m_classOf;
    std::vector<std::pair<const sa::ClassType *, sem::Class *>>     m_classes;
    std::unordered_map<const sa::SubroutineSymbol *, sem::Subroutine *>  m_subs;
    std::vector<std::pair<const sa::SubroutineSymbol *, sem::Subroutine *>>  m_bodies;
    std::vector<std::pair<const sa::VariableSymbol *, sem::Variable *>>  m_inits;
    std::vector<std::pair<const sa::Type *, sem::Typedef *>>        m_typedefs;
    std::vector<std::pair<const sa::Type *, sem::Variable *>>       m_vars;
    std::vector<std::pair<const sa::Type *, sem::Subroutine *>>     m_returns;
    std::unordered_map<const sa::Type *, sem::TypeRef *>            m_types;
    std::vector<std::pair<const sa::Scope *, sem::IDecl *>>         m_imports;
};

}

std::unique_ptr<sem::IDesign> lower(sa::Compilation &c, const LowerHost &host) {
    Lowerer l(c, host);
    return l.run();
}

}
}
