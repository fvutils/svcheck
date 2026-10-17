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
 * `svcheck --rules FILE ... [slang's file arguments]` (svcheck-plan P1.5,
 * P2.3): the rules of each FILE over the compilation, findings as text,
 * JSON, SARIF or GitHub annotations on stdout. `--contexts` prints the
 * classify stage's units instead. `--stats` (P1.7) adds each phase's time
 * and the heap in use after it: to stderr, or as `stats` in the JSON output.
 *
 * The pipeline (P2.3; pssparser lint-core-plan C7): selection (profile,
 * select, ignore, per-path and per-context overrides, from `--config` and
 * the command line), the run, slang's warnings as SLANG### markers
 * (Q-UVM4), then the engine's finish(): `[severity]`, source waivers
 * (`// svcheck waive ID: reason`) and the `-W` policy.
 *
 * The query modes need no sources: `--list-markers`, `--explain RULE`,
 * `--check-rules` (each rule's examples), `--rules-for` (the rules each
 * file gets) and `--show-config`.
 *
 * Exit status: 0 no findings; 1 findings of warning or error severity, or
 * a compilation with errors, or a failed example; 2 a rule file or
 * configuration with errors, or bad usage; 3 an internal error.
 */
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "Schema.h"

namespace slang {
namespace driver {
class Driver;
}
}

namespace svcheck {
namespace cli {

struct LintOptions {
    std::vector<std::string>    rules;          // rule files or directories, in load order
    std::string                 format = "text";    // text, json, sarif, github
    host::SemanticLayer         layer = host::SemanticLayer::Model;
    std::vector<std::string>    libraryDirs;    // code under these is library code
    bool                        contexts = false;   // print units, not findings
    bool                        stats = false;      // where the time and memory went

    // Selection and policy (P2.3).
    std::string                 config;         // --config FILE; "" for none
    std::string                 profile;        // --profile; "" for the config's
    std::vector<std::string>    select;         // --select, the command line's layer
    std::vector<std::string>    ignore;         // --ignore
    bool                        werrorAll = false;  // -Werror
    std::vector<std::string>    werror;         // -Werror=SEL ("info" too)
    std::vector<std::string>    wnoError;       // -Wno-error=SEL
    bool                        noWarnings = false; // --no-warnings
    bool                        showWaived = false; // --show-waived
    bool                        statistics = false; // --statistics: counts per code

    // Query modes.
    bool                        listMarkers = false;
    std::string                 explain;
    bool                        checkRules = false;
    bool                        rulesFor = false;
    bool                        showConfig = false;
    std::string                 uvmDir;         // --uvm DIR: examples compile against it

    bool query() const {
        return listMarkers || !explain.empty() || checkRules || rulesFor || showConfig;
    }
};

/**
 * Takes svcheck's own `-W` forms out of `args` into `opts` (pssparser's
 * warning policy): `-Werror`, `-Werror=SEL`, `-Wno-error=SEL`, and the same
 * as two arguments (`-W error=SEL`). Every other `-W...` is slang's.
 * `-Wno-error=X` where `slangName(X)` holds (a slang diagnostic option or
 * group) is also left in `args` for slang: it keeps an error slang lets be
 * downgraded (`range-width-oob`, ...) a warning.
 */
void takeWarningPolicy(std::vector<std::string> &args, LintOptions &opts,
                       const std::function<bool(const std::string &)> &slangName = {});

/** Whether `name` is an output format: text, json, sarif, github. */
bool isFormat(const std::string &name);

/**
 * Lints. `driver` has parsed its command line, not yet processed its
 * options. Returns the exit status.
 */
int runLint(slang::driver::Driver &driver, const LintOptions &opts);

}
}
