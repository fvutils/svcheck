/*
 * Lint.h
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
 * What `svcheck --rules` lints (svcheck-plan P1.5): the compilation slang's
 * driver made of the command line's files (`-f`, `+incdir+`, `+define+`;
 * R-4), as the engine's roots and run context.
 *
 * Scopes (design §3.3): a rule needs the compilation when it calls a
 * library function above level L0, matches or tests a semantic-layer kind,
 * or has `applies_to` (contexts are classified on the model). When the
 * compilation has errors, those rules are **not run**, each reported once;
 * the rest run on the syntax trees of the files that parsed cleanly.
 */
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "Reflect.h"
#include "Schema.h"
#include "run/Context.h"

namespace slang {
namespace driver {
class Driver;
}
}

namespace svcheck {
namespace lint {
namespace rules {
class Rule;
}
}

namespace host {

namespace rf = svcheck::ast::reflect;

class SvContext;

class LintCompilation {
public:
    /**
     * Compiles the driver's files (parsed already, parseAllSources()) with
     * the semantic layer installed. Library code is code in `libraryDirs`
     * and in the directory of the file declaring package `uvm_pkg`; its
     * syntax trees are not roots, and the lowering marks it (`inLibrary`).
     */
    LintCompilation(slang::driver::Driver &driver, SemanticLayer layer,
        const std::vector<std::string> &libraryDirs);
    ~LintCompilation();

    /** The roots to lint: the layer's root, when it compiled, then the syntax trees. */
    const std::vector<rf::Node> &roots() const { return m_roots; }

    /** The run's context: the model's (SvContext) when it compiled, else a plain one. */
    lint::run::Context &context() { return *m_context; }

    /** The model's context; null when there is no model. */
    SvContext *svContext() const { return m_sv; }

    /** The compilation's errors, which slang reported. */
    uint32_t errors() const { return m_errors; }

    /** The files with syntax errors, which no rule runs on. */
    const std::vector<std::string> &unparsed() const { return m_unparsed; }

    /**
     * Each file's path, by fileid: the fileids the findings carry. It grows
     * as the run locates nodes in files it has not seen (an `include`d file).
     */
    const std::vector<std::string> &paths() const;

    /** The library directories, as absolute paths. */
    const std::vector<std::string> &libraryDirs() const { return m_libraryDirs; }

private:
    struct Impl;
    std::unique_ptr<Impl>                       m_impl;
    std::vector<rf::Node>                       m_roots;
    std::unique_ptr<lint::run::Context>         m_context;
    SvContext                                   *m_sv = nullptr;
    uint32_t                                    m_errors = 0;
    std::vector<std::string>                    m_unparsed;
    std::vector<std::string>                    m_libraryDirs;
};

/** Whether rule `r` needs the compilation (design §3.3), so cannot run without it. */
bool needsCompilation(const lint::rules::Rule &r);

}
}
