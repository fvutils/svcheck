/*
 * LintEngine.h
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
#include <vector>
#include "svcheck/lint/ILintEngine.h"
#include <set>
#include <unordered_map>
#include "pipeline/Selection.h"
#include "rules/Rule.h"
#include "rules/RuleLoader.h"
#include "run/Classify.h"
#include "run/Runner.h"

namespace svcheck {
namespace lint {

class LintEngine : public virtual ILintEngine {
public:
    /** The format version of pss-rules-format.md. */
    static constexpr int32_t RuleFormatVersion = 1;

    LintEngine();
    virtual ~LintEngine();

    virtual int32_t ruleFormatVersion() const override { return RuleFormatVersion; }

    virtual uint32_t numRuleSets() const override {
        return static_cast<uint32_t>(m_rulesets.size());
    }

    virtual const IRuleSet *getRuleSet(uint32_t idx) const override {
        return (idx < m_rulesets.size()) ? m_rulesets[idx].get() : nullptr;
    }

    virtual const IRuleSet *loadRules(const LintSource &src, bool builtin=false) override;

    virtual uint32_t loadBuiltinRules() override;

    virtual uint32_t numBuiltinRuleFiles() const override;

    virtual RuleProbe probeRules(const std::string &text) const override;

    virtual const std::vector<RuleDiagnostic> &diagnostics() const override {
        return m_diagnostics;
    }

    virtual std::string diagnosticsJson() const override;

    virtual std::string rulesJson() const override;

    virtual std::string run(const std::vector<ast::reflect::Node> &roots,
        const std::string &requestJson, run::Context *ctx=nullptr) override;

    virtual std::string setHostMarkers(const std::string &json) override;

    virtual std::string resolveSelection(const std::string &json) override;

    virtual std::string collectWaivers(const std::vector<LintTokens> &files) override;

    virtual std::string applyWaivers(const std::string &requestJson) override;

    virtual std::string finish(const std::string &requestJson) override;

    virtual std::string catalogJson() const override;

    virtual std::string coreCode(const std::string &message) const override;

    /** Every loaded rule, in load order: what the runner (C5) dispatches. */
    std::vector<const rules::Rule *> ruleList() const;

    /** The loaded files' `contexts:` classifiers, in load order, each once. */
    std::vector<const rules::Classifier *> classifiers() const;

    /**
     * The classify stage (run/Classify.h) over `roots`, which it indexes in
     * `ctx`: once per context, so run() does not repeat it. Nothing when
     * no classifiers are loaded.
     */
    run::Contexts classify(const std::vector<ast::reflect::Node> &roots, run::Context &ctx,
        uint64_t maxSteps=1000000);

    /** How the files a rule file's `uses:` names are read; none by default. */
    void setReader(rules::FileReader reader) { m_reader = std::move(reader); }

private:
    // m_hostMarkers back to the embedded catalogue's markers.
    void resetHostMarkers();

    // A run's "selection" (C7.1): the rules some owned file enables, in load
    // order, with each file's set and options put in `opts`.
    std::vector<const rules::Rule *> select(const yaml::Node &selection,
        const std::vector<ast::reflect::Node> &roots, const std::vector<const rules::Rule *> &all,
        const run::Context &ctx, run::RunOptions &opts, std::vector<std::string> &problems);

private:
    std::vector<rules::RuleSetUP>   m_rulesets;
    rules::FileReader               m_reader;
    std::vector<RuleDiagnostic>     m_diagnostics;
    rules::RuleClaims               m_claims;          // IDs and names, across files
    bool                            m_builtinLoaded = false;
    // The embedded catalogue's markers, then setHostMarkers()'s: a host
    // entry replaces a core one where it stands. m_fromHost marks the host's.
    std::vector<pipeline::MarkerMeta>   m_hostMarkers;
    std::unordered_map<std::string, size_t> m_hostIndex;
    std::vector<bool>               m_fromHost;
    std::set<std::string>           m_refused;
};

}
}
