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
#include "svcheck/lint/ILintTestHooks.h"

namespace svcheck {
namespace lint {

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
     * One hook call as JSON (svcheck-plan P1.1): {"op": "<hook>", "args":
     * [...]}, the hook's arguments in order. Returns the hook's JSON, or
     * {"ok": false, "error": "..."} for a request that names no hook or has
     * the wrong arguments. What `svcheck --engine-test-hooks` serves, one
     * request per line, so the Python tests drive the shipped binary.
     */
    std::string dispatch(const std::string &requestJson);
};

}
}
