/*
 * LintTestHooks.h
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
#pragma once
#include <functional>
#include <memory>
#include <vector>
#include "svcheck/lint/ILintTestHooks.h"
#include "Reflect.h"
#include "run/Examples.h"

namespace svcheck {
namespace lint {

/**
 * The trees of a source text, which the host parses for the node hooks
 * (svcheck-plan P1.2).
 */
using HookTrees = run::Trees;

/** Parses `source` into `out`; false, with `error`, when it cannot. */
using HookParser = std::function<bool(const std::string &source, HookTrees &out,
    std::string &error)>;

class LintTestHooks : public virtual ILintTestHooks {
public:
    LintTestHooks();
    virtual ~LintTestHooks();

    virtual std::string yamlLoad(const std::string &name, const std::string &text) override;
    virtual std::string yamlSourceMaps(const std::string &name, const std::string &text) override;
    virtual std::string yamlCelParse(
        const std::string &name, const std::string &text, const std::string &key) override;
    virtual std::string celParse(const std::string &text) override;
    virtual std::string celEval(
        const std::string &text, const std::string &bindings, uint64_t max_steps) override;
    virtual std::string celFunctions() override;
    virtual std::string celCheck(const std::string &name, const std::string &text) override;
    virtual std::string ruleLoad(const std::string &name, const std::string &text,
        bool builtin) override;
    virtual std::string rulesSchema() override;
    virtual std::string regexSearch(const std::string &pattern, const std::string &text) override;
    virtual std::string reflectSchema() override;

    /**
     * The node hooks: as pssparser's, over the trees the host's parser
     * makes of `source` (setParser). Without a parser they refuse.
     *
     * - celEvalNode: evaluates `text` with `node` bound to the first root.
     * - celCheckNode: checks a rule-shaped document with `node` typed by
     *   the first root's class.
     * - lintWalk: the rules' matches, in walk order, over every root.
     * - lintRun: a whole run of the rules enabled (all, or `enabled`).
     */
    std::string celEvalNode(const std::string &source, const std::string &text,
        const std::string &bindings, uint64_t max_steps);
    std::string celCheckNode(const std::string &source, const std::string &name,
        const std::string &text);
    std::string lintWalk(const std::string &source, const std::string &name,
        const std::string &text);
    std::string lintRun(const std::string &source, const std::string &name,
        const std::string &text, bool allEnabled, const std::vector<std::string> &enabled,
        uint64_t maxSteps, uint32_t gateErrors);

    /**
     * Runs the examples (run/Examples.h) of the rules enabled (all, or
     * `enabled`) in rule file `name`, compiling them with the host's
     * compiler (setCompiler); without one it refuses.
     */
    std::string lintExamples(const std::string &name, const std::string &text, bool allEnabled,
        const std::vector<std::string> &enabled);

    /** The host's parser for the node hooks. */
    void setParser(HookParser parser) { m_parser = std::move(parser); }

    /** The host's compiler for the examples hook. */
    void setCompiler(run::Compiler compiler) { m_compiler = std::move(compiler); }

    /**
     * One hook call as JSON (svcheck-plan P1.1): {"op": "<hook>", "args":
     * [...]}, the hook's arguments in order. Returns the hook's JSON, or
     * {"ok": false, "error": "..."} for a request that names no hook or has
     * the wrong arguments. What `svcheck --engine-test-hooks` serves, one
     * request per line, so the Python tests drive the shipped binary.
     */
    std::string dispatch(const std::string &requestJson);

private:
    bool parse(const std::string &source, HookTrees &out, std::string &result);

    HookParser          m_parser;
    run::Compiler       m_compiler;
};

}
}
