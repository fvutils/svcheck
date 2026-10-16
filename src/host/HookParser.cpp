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
#include "Syntax.h"
#include "run/Context.h"
#include "slang/ast/Compilation.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceManager.h"

namespace svcheck {
namespace host {

namespace {

// What keeps a hook's tree alive, and its locations readable.
struct Parsed {
    slang::SourceManager                    sm;
    std::shared_ptr<slang::syntax::SyntaxTree> tree;
    std::unique_ptr<Sources>                sources;

    ~Parsed() {
        setSources(nullptr);
    }
};

// The same, for a compiled source. The source manager is shared by every
// compilation, so uvm_pkg's tree is parsed once.
struct Compiled {
    std::shared_ptr<slang::syntax::SyntaxTree>     tree;
    std::unique_ptr<slang::ast::Compilation>        comp;
    std::unique_ptr<sem::IDesign>                   design;
    std::unique_ptr<Sources>                        sources;

    ~Compiled() {
        setSources(nullptr);
        setSlangSources(nullptr);
    }
};

void setFiles(lint::HookTrees &out, const Sources &sources) {
    out.context.reset(new lint::run::Context());
    std::unordered_map<int32_t, std::string> files;
    for (size_t i=0; i<sources.paths().size(); i++) {
        files[static_cast<int32_t>(i)] = sources.paths()[i];
    }
    out.context->setFiles(files, "");
}

}

lint::HookParser syntaxHookParser() {
    return [](const std::string &source, lint::HookTrees &out, std::string &error) {
        auto p = std::make_shared<Parsed>();
        p->tree = slang::syntax::SyntaxTree::fromFileInMemory(source, p->sm, "snippet.sv", "snippet.sv");
        if (!p->tree) {
            error = "slang made no tree of the source";
            return false;
        }
        p->sources.reset(new Sources(p->sm));
        setSources(p->sources.get());
        const slang::syntax::SyntaxNode &root = p->tree->root();
        p->sources->fileid(root.getFirstToken().location().buffer());
        out.roots.push_back(syntaxNode(&root));
        setFiles(out, *p->sources);
        out.keep = p;
        return true;
    };
}

lint::HookParser semanticHookParser(SemanticLayer layer, const std::string &uvmDir) {
    auto sm = std::make_shared<slang::SourceManager>();
    if (!uvmDir.empty()) {
        sm->addUserDirectories(uvmDir);
    }
    auto uvm = std::make_shared<std::shared_ptr<slang::syntax::SyntaxTree>>();
    return [layer, uvmDir, sm, uvm](const std::string &source, lint::HookTrees &out,
            std::string &error) {
        auto p = std::make_shared<Compiled>();
        // By name only: the shared source manager takes a path once.
        p->tree = slang::syntax::SyntaxTree::fromText(source, *sm, "snippet.sv");
        if (!p->tree) {
            error = "slang made no tree of the source";
            return false;
        }
        p->comp.reset(new slang::ast::Compilation());
        bool withUvm = !uvmDir.empty() && source.find("uvm_") != std::string::npos;
        if (withUvm) {
            if (!*uvm) {
                auto t = slang::syntax::SyntaxTree::fromFile(uvmDir + "/uvm_pkg.sv", *sm);
                if (!t) {
                    error = "cannot read " + uvmDir + "/uvm_pkg.sv";
                    return false;
                }
                *uvm = *t;
            }
            p->comp->addSyntaxTree(*uvm);
        }
        p->comp->addSyntaxTree(p->tree);
        for (const slang::Diagnostic &d : p->comp->getAllDiagnostics()) {
            if (d.isError()) {
                error = "the source does not compile: "
                    + slang::DiagnosticEngine::reportAll(*sm, std::span(&d, 1));
                return false;
            }
        }

        p->sources.reset(new Sources(*sm));
        setSources(p->sources.get());
        const slang::syntax::SyntaxNode &root = p->tree->root();
        p->sources->fileid(root.getFirstToken().location().buffer(), "snippet.sv");
        std::vector<std::string> lib;
        if (withUvm) {
            lib.push_back(uvmDir);
        }
        if (layer == SemanticLayer::Model) {
            p->design = lowerCompilation(*p->comp, *p->sources, lib);
            out.roots.push_back(semNode(p->design.get()));
        } else if (layer == SemanticLayer::Slang) {
            setSlangSources(p->sources.get(), sm.get(), lib);
            out.roots.push_back(slangRoot(*p->comp));
        }
        out.roots.push_back(syntaxNode(&root));
        setFiles(out, *p->sources);
        out.keep = p;
        return true;
    };
}

}
}
