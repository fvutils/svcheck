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
 * the rest run on the syntax trees, and their findings are shown only in
 * files without a syntax error.
 */
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "Reflect.h"
#include "Schema.h"
#include "run/Context.h"
#include "svcheck/lint/LintRequest.h"

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

namespace sem {
class IDesign;
}

namespace host {

namespace rf = svcheck::ast::reflect;

class SvContext;

/**
 * Bytes of heap in use now: glibc's arena and mmapped blocks (mallinfo2,
 * or mallinfo before 2.33); 0 where that is not known. For `--stats`
 * (svcheck-plan P1.7).
 */
size_t heapInUse();

/** Peak resident set size so far, in bytes; 0 where that is not known. */
size_t peakRss();

class LintCompilation {
public:
    /** Where the constructor's time and memory went (`--stats`). */
    struct Stats {
        double  elaborateS = 0;     // createCompilation and every diagnostic
        double  lowerS = 0;         // the lowering
        size_t  heapElaborated = 0; // heapInUse() after elaborating
        size_t  heapLowered = 0;    // and after lowering
    };

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

    /** The model's root; null when there is no model. */
    sem::IDesign *design() const;

    /** The model's context; null when there is no model. */
    SvContext *svContext() const { return m_sv; }

    /** The compilation's errors, which slang reported. */
    uint32_t errors() const { return m_errors; }

    /**
     * The user files with syntax errors, as absolute paths: their findings
     * are not shown. A tree with no other user file is not linted.
     */
    const std::vector<std::string> &unparsed() const { return m_unparsed; }

    /**
     * Whether a finding in file `path` is shown: not when the file has a
     * syntax error, nor when it is library code (S-6).
     */
    bool shows(const std::string &path) const;

    /**
     * Each file's path, by fileid: the fileids the findings carry. It grows
     * as the run locates nodes in files it has not seen (an `include`d file).
     */
    const std::vector<std::string> &paths() const;

    /**
     * Every user file the compilation read, as an absolute path, sorted:
     * the command line's and those they `include`; not library code.
     */
    const std::vector<std::string> &files() const { return m_files; }

    /** The library directories, as absolute paths. */
    const std::vector<std::string> &libraryDirs() const { return m_libraryDirs; }

    /** A warning slang reported in user code (Q-UVM4: a SLANG### marker). */
    struct Warning {
        std::string     option;     // slang's option name: the marker's name
        std::string     message;
        std::string     file;       // as paths() names it
        int32_t         line = 0;
        int32_t         col = 0;
    };

    /**
     * slang's warnings, at the severity its options give them, in files
     * whose findings are shown (shows()); one per (option, place), sorted
     * by place (svcheck-plan P0-1, P1-42: slang's list varies between runs).
     */
    const std::vector<Warning> &warnings() const { return m_warnings; }

    /**
     * Each user file's tokens, for source waivers (C9.3): its line comments
     * with their text, its other tokens as positions; lexed as written, so
     * a macro's use and an `include`d file's own text are seen. Files are
     * named as paths() names them.
     */
    std::vector<lint::LintTokens> waiverTokens() const;

    const Stats &stats() const { return m_stats; }

private:
    struct Impl;
    std::unique_ptr<Impl>                       m_impl;
    std::vector<rf::Node>                       m_roots;
    std::unique_ptr<lint::run::Context>         m_context;
    SvContext                                   *m_sv = nullptr;
    uint32_t                                    m_errors = 0;
    std::vector<std::string>                    m_unparsed;
    std::vector<std::string>                    m_files;
    std::vector<std::string>                    m_libraryDirs;
    std::vector<Warning>                        m_warnings;
    Stats                                       m_stats;
};

/** Whether rule `r` needs the compilation (design §3.3), so cannot run without it. */
bool needsCompilation(const lint::rules::Rule &r);

}
}
