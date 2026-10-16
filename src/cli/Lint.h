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
 * `svcheck --rules FILE ... [slang's file arguments]` (svcheck-plan P1.5):
 * the rules of each FILE over the compilation, findings as text, JSON or
 * SARIF on stdout. `--contexts` prints the classify stage's units instead.
 * `--stats` (P1.7) adds each phase's time and the heap in use after it: to
 * stderr, or as `stats` in the JSON output.
 *
 * Exit status: 0 no findings; 1 findings of warning or error severity, or
 * a compilation with errors; 2 a rule file with errors, or bad usage.
 */
#pragma once
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
    std::vector<std::string>    rules;          // rule files, in load order
    std::string                 format = "text";    // text, json, sarif
    host::SemanticLayer         layer = host::SemanticLayer::Model;
    std::vector<std::string>    libraryDirs;    // code under these is library code
    bool                        contexts = false;   // print units, not findings
    bool                        stats = false;      // where the time and memory went
};

/** Whether `name` is an output format. */
bool isFormat(const std::string &name);

/**
 * Lints. `driver` has parsed its command line, not yet processed its
 * options. Returns the exit status.
 */
int runLint(slang::driver::Driver &driver, const LintOptions &opts);

}
}
