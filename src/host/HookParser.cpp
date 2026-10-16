/*
 * HookParser.cpp
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
#include <memory>
#include "HookParser.h"
#include "SemModel.h"
#include "SlangSchema.h"
#include "SvContext.h"
#include "Syntax.h"
#include "run/Context.h"
#include "slang/ast/Compilation.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceManager.h"

namespace svcheck {
namespace host {

namespace {

using lint::run::CompileError;
using lint::run::SourceFile;
using lint::run::Trees;

// What keeps the trees alive, and their locations readable. A compiled
// source shares its source manager with every other, so uvm_pkg's tree is
// parsed once; a parsed one has its own.
struct Compiled {
    std::shared_ptr<slang::SourceManager>                   sm;
    std::vector<std::shared_ptr<slang::syntax::SyntaxTree>> trees;
    std::unique_ptr<slang::ast::Compilation>                comp;
    std::unique_ptr<sem::IDesign>                           design;
    std::unique_ptr<Sources>                                sources;

    ~Compiled() {
        setSources(nullptr);
        setSlangSources(nullptr);
    }
};

// The trees' context: the model's, which makes the classify stage's units,
// or a plain one.
void setFiles(Trees &out, const Sources &sources, sem::IDesign *design) {
    out.context.reset(design ? new SvContext(design) : new lint::run::Context());
    std::unordered_map<int32_t, std::string> files;
    for (size_t i=0; i<sources.paths().size(); i++) {
        files[static_cast<int32_t>(i)] = sources.paths()[i];
    }
    out.context->setFiles(files, "");
}

void toError(const slang::SourceManager &sm, const slang::Diagnostic &d, CompileError &error) {
    slang::DiagnosticEngine engine(sm);
    error.message = engine.formatMessage(d);
    if (d.location.valid()) {
        slang::SourceLocation loc = sm.getFullyOriginalLoc(d.location);
        error.file = std::string(sm.getFileName(loc));
        error.line = static_cast<int32_t>(sm.getLineNumber(loc));
        error.col = static_cast<int32_t>(sm.getColumnNumber(loc));
    }
}

}

lint::run::Compiler hostCompiler(SemanticLayer layer, const std::string &uvmDir, bool strict) {
    auto shared = std::make_shared<slang::SourceManager>();
    if (!uvmDir.empty()) {
        shared->addUserDirectories(uvmDir);
    }
    auto uvm = std::make_shared<std::shared_ptr<slang::syntax::SyntaxTree>>();
    return [layer, uvmDir, strict, shared, uvm](const std::vector<SourceFile> &files, Trees &out,
            CompileError &error) {
        auto p = std::make_shared<Compiled>();
        bool semantic = layer != SemanticLayer::None;
        p->sm = semantic ? shared : std::make_shared<slang::SourceManager>();
        bool mentionsUvm = false;
        for (const SourceFile &f : files) {
            // By name only: the shared source manager takes a path once.
            auto t = semantic ? slang::syntax::SyntaxTree::fromText(f.text, *p->sm, f.name)
                : slang::syntax::SyntaxTree::fromFileInMemory(f.text, *p->sm, f.name, f.name);
            if (!t) {
                error.message = "slang made no tree of " + f.name;
                return false;
            }
            if (strict && !semantic) {
                for (const slang::Diagnostic &d : t->diagnostics()) {
                    if (d.isError()) {
                        toError(*p->sm, d, error);
                        return false;
                    }
                }
            }
            p->trees.push_back(t);
            mentionsUvm |= f.text.find("uvm_") != std::string::npos;
        }

        p->sources.reset(new Sources(*p->sm));
        setSources(p->sources.get());
        for (size_t i=0; i<files.size(); i++) {
            const slang::syntax::SyntaxNode &root = p->trees[i]->root();
            p->sources->fileid(root.getFirstToken().location().buffer(), files[i].name);
        }
        if (semantic) {
            p->comp.reset(new slang::ast::Compilation());
            bool withUvm = !uvmDir.empty() && mentionsUvm;
            if (withUvm) {
                if (!*uvm) {
                    auto t = slang::syntax::SyntaxTree::fromFile(uvmDir + "/uvm_pkg.sv", *p->sm);
                    if (!t) {
                        error.message = "cannot read " + uvmDir + "/uvm_pkg.sv";
                        return false;
                    }
                    *uvm = *t;
                }
                p->comp->addSyntaxTree(*uvm);
            }
            for (const auto &t : p->trees) {
                p->comp->addSyntaxTree(t);
            }
            for (const slang::Diagnostic &d : p->comp->getAllDiagnostics()) {
                if (d.isError()) {
                    toError(*p->sm, d, error);
                    return false;
                }
            }
            std::vector<std::string> lib;
            if (withUvm) {
                lib.push_back(uvmDir);
            }
            if (layer == SemanticLayer::Model) {
                p->design = lowerCompilation(*p->comp, *p->sources, lib);
                out.roots.push_back(semNode(p->design.get()));
            } else if (layer == SemanticLayer::Slang) {
                setSlangSources(p->sources.get(), p->sm.get(), lib);
                out.roots.push_back(slangRoot(*p->comp));
            }
        }
        for (const auto &t : p->trees) {
            out.roots.push_back(syntaxNode(&t->root()));
        }
        setFiles(out, *p->sources, p->design.get());
        out.keep = p;
        return true;
    };
}

lint::HookParser hookParser(lint::run::Compiler compile) {
    return [compile](const std::string &source, lint::HookTrees &out, std::string &error) {
        CompileError e;
        if (compile({{"snippet.sv", source}}, out, e)) {
            return true;
        }
        error = e.line ? "the source does not compile: " + e.file + ":" + std::to_string(e.line)
            + ":" + std::to_string(e.col) + ": " + e.message : e.message;
        return false;
    };
}

}
}
