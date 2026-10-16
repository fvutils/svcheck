/*
 * Lint.cpp
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
#include <algorithm>
#include <chrono>
#include <filesystem>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#if !defined(__wasi__)
#include <sys/resource.h>
#endif
#include "Lint.h"
#include "SemModel.h"
#include "SvContext.h"
#include "Syntax.h"
#include "rules/Rule.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/driver/Driver.h"
#include "slang/parsing/Preprocessor.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceManager.h"

namespace svcheck {
namespace host {

namespace fs = std::filesystem;

struct LintCompilation::Impl {
    std::unique_ptr<slang::ast::Compilation>    comp;
    std::unique_ptr<sem::IDesign>               design;
    std::unique_ptr<Sources>                    sources;
};

namespace {

std::string absolute(const std::string &path) {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::absolute(path), ec);
    return ec ? path : p.string();
}

bool under(const std::string &path, const std::vector<std::string> &dirs) {
    for (const std::string &d : dirs) {
        if (path.size() > d.size() && path.compare(0, d.size(), d) == 0
                && (path[d.size()] == '/' || d.back() == '/')) {
            return true;
        }
    }
    return false;
}

}

size_t heapInUse() {
#if defined(__GLIBC__) && (__GLIBC__ > 2 || __GLIBC_MINOR__ >= 33)
    struct mallinfo2 mi = mallinfo2();
    return mi.uordblks + mi.hblkhd;
#elif defined(__GLIBC__)
    // manylinux_2_28's glibc has only mallinfo, whose int fields wrap at 2 GB.
    struct mallinfo mi = mallinfo();
    return static_cast<size_t>(static_cast<unsigned>(mi.uordblks))
        + static_cast<size_t>(static_cast<unsigned>(mi.hblkhd));
#else
    return 0;
#endif
}

size_t peakRss() {
#if defined(__wasi__)
    return 0;           // WASI's emulated rusage has no ru_maxrss
#else
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) {
        return 0;
    }
#if defined(__APPLE__)
    return static_cast<size_t>(ru.ru_maxrss);
#else
    return static_cast<size_t>(ru.ru_maxrss) * 1024;
#endif
#endif
}

LintCompilation::LintCompilation(slang::driver::Driver &driver, SemanticLayer layer,
        const std::vector<std::string> &libraryDirs) : m_impl(new Impl()) {
    using Clock = std::chrono::steady_clock;
    auto secs = [](Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double>(b - a).count();
    };
    Impl &p = *m_impl;
    auto t0 = Clock::now();
    p.comp = driver.createCompilation();
    // slang's warnings are not findings (Q-UVM4, P2.2); its errors gate,
    // as its own command line ranks them (-Werror and the like included).
    for (const slang::Diagnostic &d : p.comp->getAllDiagnostics()) {
        slang::DiagnosticSeverity sev = driver.diagEngine.getSeverity(d.code, d.location);
        if (sev == slang::DiagnosticSeverity::Error || sev == slang::DiagnosticSeverity::Fatal) {
            driver.diagEngine.issue(d);
        }
    }
    m_errors = static_cast<uint32_t>(driver.diagEngine.getNumErrors());
    m_stats.elaborateS = secs(t0, Clock::now());
    m_stats.heapElaborated = heapInUse();

    for (const std::string &d : libraryDirs) {
        m_libraryDirs.push_back(absolute(d));
    }
    // UVM is library code wherever it is: the directory holding uvm_pkg.
    if (const slang::ast::PackageSymbol *uvm = p.comp->getPackage("uvm_pkg")) {
        slang::SourceLocation loc = driver.sourceManager.getFullyOriginalLoc(uvm->location);
        if (loc.valid()) {
            std::string dir = absolute(
                driver.sourceManager.getFullPath(loc.buffer()).parent_path().string());
            if (std::find(m_libraryDirs.begin(), m_libraryDirs.end(), dir)
                    == m_libraryDirs.end()) {
                m_libraryDirs.push_back(dir);
            }
        }
    }

    // Every source file read: each tree's own, and those they include.
    auto addFile = [&](slang::BufferID buf) {
        std::string path = driver.sourceManager.getFullPath(buf).string();
        if (!path.empty()) {
            path = absolute(path);
            if (!under(path, m_libraryDirs)) {
                m_files.push_back(path);
            }
        }
    };
    for (const auto &t : driver.syntaxTrees) {
        for (slang::BufferID buf : t->getSourceBufferIds()) {
            addFile(buf);
        }
        for (const slang::parsing::IncludeMetadata &inc : t->getIncludeDirectives()) {
            if (inc.buffer.id) {
                addFile(inc.buffer.id);
            }
        }
    }
    std::sort(m_files.begin(), m_files.end());
    m_files.erase(std::unique(m_files.begin(), m_files.end()), m_files.end());

    p.sources.reset(new Sources(driver.sourceManager));
    setSources(p.sources.get());
    // The user's files, in command-line order; a library's are not linted.
    // A tree is linted when it has a user file without a syntax error: with
    // --single-unit one tree holds every file, the library's too. Findings
    // in a file with a syntax error, or in library code, are not shown
    // (shows()).
    // A tree that holds library files too (--single-unit) is linted from
    // its user members, so the walk and the indexes skip the library's
    // syntax (svcheck-plan P1.7: uvm_pkg was half of OpenTitan's walk).
    std::vector<std::pair<const slang::syntax::SyntaxTree *, bool>> trees;
    for (const auto &t : driver.syntaxTrees) {
        std::vector<std::string> mine;
        bool mixed = false;
        for (slang::BufferID buf : t->getSourceBufferIds()) {
            std::string path = absolute(driver.sourceManager.getFullPath(buf).string());
            if (!under(path, m_libraryDirs)) {
                p.sources->fileid(buf);
                mine.push_back(path);
            } else {
                mixed = true;
            }
        }
        if (mine.empty()) {
            continue;
        }
        for (const slang::Diagnostic &d : t->diagnostics()) {
            if (!d.isError()) {
                continue;
            }
            // An error with no place spoils every user file of the tree.
            slang::SourceLocation loc = driver.sourceManager.getFullyOriginalLoc(d.location);
            std::vector<std::string> bad = mine;
            if (loc.valid()) {
                bad = {absolute(driver.sourceManager.getFullPath(loc.buffer()).string())};
            }
            for (const std::string &path : bad) {
                if (!under(path, m_libraryDirs) && std::find(m_unparsed.begin(),
                        m_unparsed.end(), path) == m_unparsed.end()) {
                    m_unparsed.push_back(path);
                }
            }
        }
        if (std::any_of(mine.begin(), mine.end(), [this](const std::string &f) {
                return std::find(m_unparsed.begin(), m_unparsed.end(), f) == m_unparsed.end();
            })) {
            trees.emplace_back(t.get(), mixed);
        }
    }

    auto tLower = Clock::now();
    if (!m_errors && layer == SemanticLayer::Model) {
        p.design = lowerCompilation(*p.comp, *p.sources, m_libraryDirs);
        m_roots.push_back(semNode(p.design.get()));
    }
    m_stats.lowerS = secs(tLower, Clock::now());
    m_stats.heapLowered = heapInUse();
    for (const auto &[t, mixed] : trees) {
        const slang::syntax::SyntaxNode &root = t->root();
        if (!mixed || root.kind != slang::syntax::SyntaxKind::CompilationUnit) {
            m_roots.push_back(syntaxNode(&root));
            continue;
        }
        for (const slang::syntax::MemberSyntax *m
                : root.as<slang::syntax::CompilationUnitSyntax>().members) {
            slang::SourceLocation loc = driver.sourceManager.getFullyOriginalLoc(
                m->getFirstToken().location());
            if (!loc.valid() || !under(absolute(
                    driver.sourceManager.getFullPath(loc.buffer()).string()), m_libraryDirs)) {
                m_roots.push_back(syntaxNode(m));
            }
        }
    }

    if (p.design) {
        m_sv = new SvContext(p.design.get());
        m_context.reset(m_sv);
    } else {
        m_context.reset(new lint::run::Context());
    }
    std::unordered_map<int32_t, std::string> files;
    for (size_t i=0; i<p.sources->paths().size(); i++) {
        files[static_cast<int32_t>(i)] = p.sources->paths()[i];
    }
    m_context->setFiles(files, "");
}

LintCompilation::~LintCompilation() {
    m_context.reset();
    setSources(nullptr);
}

bool LintCompilation::shows(const std::string &path) const {
    std::string a = absolute(path);
    return !under(a, m_libraryDirs)
        && std::find(m_unparsed.begin(), m_unparsed.end(), a) == m_unparsed.end();
}

const std::vector<std::string> &LintCompilation::paths() const {
    return m_impl->sources->paths();
}

bool needsCompilation(const lint::rules::Rule &r) {
    if (r.m_level >= lint::cel::Level::L1 || !r.m_appliesTo.empty()) {
        return true;
    }
    auto semantic = [](const lint::rules::KindSet &k) {
        return std::any_of(k.classes.begin(), k.classes.end(), isSemanticClass);
    };
    return semantic(r.m_match) || semantic(r.m_inside.kind) || semantic(r.m_notInside.kind);
}

}
}
