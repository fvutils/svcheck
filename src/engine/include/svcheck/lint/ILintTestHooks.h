/*
 * ILintTestHooks.h
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
 * PRIVATE. Test entry points into the lint engine's low-level pieces (YAML
 * layer, regex engine, CEL parser and evaluator), lint-core-plan.md §5.
 *
 * pytest is the only C++ test harness (there is no gtest target), so these
 * are how the tests reach code below the public API. Every call takes and
 * returns text: results are JSON, produced identically by the native and the
 * WASM build, which is what the parity tests compare. Not a supported API;
 * the shapes change without notice.
 */
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace svcheck {
namespace lint {

class ILintTestHooks {
public:
    virtual ~ILintTestHooks() { }

    /** {"ok":true,"root":<node>} or {"ok":false,"error":{...}} */
    virtual std::string yamlLoad(const std::string &name, const std::string &text) = 0;

    /**
     * Every string scalar with its source map (C2.2), in document order:
     * {"ok":true,"scalars":[{"line","col","style","value","exact","map":[...]}]}
     */
    virtual std::string yamlSourceMaps(const std::string &name, const std::string &text) = 0;

    /**
     * CEL-parses the string value of every mapping member named `key`, and
     * reports each syntax error at its rule-file position (C2.2):
     * {"ok":true,"results":[{"ok":true}|{"ok":false,"message","offset",
     *  "line","col","exact"}]}
     */
    virtual std::string yamlCelParse(
        const std::string &name, const std::string &text, const std::string &key) = 0;

    /** {"ok":true,"ast":<expr>,"profile_errors":[...]} or a syntax error */
    virtual std::string celParse(const std::string &text) = 0;

    /**
     * {"ok":true,"result":<value>,"steps":N,"budget_exceeded":b,
     * "calls":[overload id, ...]} (every dispatched overload, in call order) or a syntax
     * error. `bindings` is a JSON object of variables (name -> value), or
     * empty; `max_steps` 0 is the default budget.
     */
    virtual std::string celEval(
        const std::string &text, const std::string &bindings, uint64_t max_steps) = 0;

    /**
     * The function registry (C3.2), in registration order:
     * {"functions":[{"id","function","kind","receiver","params","result",
     *  "display","group","level","scope","cost","doc","in_profile","evaluator"}]}
     */
    virtual std::string celFunctions() = 0;

    /**
     * Type-checks the CEL in a small rule-shaped YAML document (C3.3), and
     * runs it if it checks. Keys: `vars` (name -> type, as signatures write
     * types), `bindings` (name -> value; typed from the value unless in
     * `vars`), `define` (helpers), `let` (in order), `expr` (with `expect`:
     * a type), `where` (must be bool), `report` (must be list(node)),
     * `max_nesting`, `max_steps`, `run` (default true).
     * {"ok":true,"errors":[{"at","offset","line","col","message"}],
     *  "types":{key:type},"overloads":{key:[id]},
     *  "values":{key:<value>},"calls":{key:[id]},"steps":{key:N}}
     * where `at` is the key (`expr`, `let:d`, `define:lo(it)`); the last
     * three only when there are no errors.
     */
    virtual std::string celCheck(const std::string &name, const std::string &text) = 0;

    /**
     * The reflection tables (C1.3): {"classes":[{"name","super","fields":[...]}],
     * "enums":[...],"structs":[...]}; class ids are positions, 0 is "none".
     */
    virtual std::string reflectSchema() = 0;

    /**
     * Loads one rule file into a fresh engine (C4.1):
     * {"loaded":b,"rules":[<rule>],"diagnostics":[<diagnostic>]}, the
     * shapes of ILintEngine::rulesJson() and diagnosticsJson(). `builtin`
     * applies the built-in rules' requirements.
     */
    virtual std::string ruleLoad(const std::string &name, const std::string &text,
        bool builtin) = 0;

    /**
     * The JSON Schema of a rule file (C4.4), written from the loader's field
     * table: what scripts/gen_rules_schema.py publishes.
     */
    virtual std::string rulesSchema() = 0;

    /** {"ok":true,"match":b,"steps":N,"size":n} or {"ok":false,"error":{...}} */
    virtual std::string regexSearch(const std::string &pattern, const std::string &text) = 0;
};

}
}
