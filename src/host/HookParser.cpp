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
#include "Syntax.h"
#include "run/Context.h"
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

}

lint::HookParser syntaxHookParser() {
    return [](const std::string &source, lint::HookTrees &out, std::string &error) {
        installSyntaxSchema();
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
        out.context.reset(new lint::run::Context());
        std::unordered_map<int32_t, std::string> files;
        for (size_t i=0; i<p->sources->paths().size(); i++) {
            files[static_cast<int32_t>(i)] = p->sources->paths()[i];
        }
        out.context->setFiles(files, "");
        out.keep = p;
        return true;
    };
}

}
}
