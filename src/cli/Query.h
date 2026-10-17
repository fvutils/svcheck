/*
 * Query.h
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
 *
 * The lint CLI's query modes (svcheck-plan P2.3; pssparser's `--list-markers`,
 * `--explain`, `--check-rules`, `--rules-for`): they read the loaded rules,
 * the catalogue and the selection, and compile no sources (but examples).
 */
#pragma once
#include <string>
#include "LintEngine.h"

namespace svcheck {
namespace cli {

struct LintOptions;

/** `--list-markers`: every marker the catalogue knows. Returns the exit status. */
int listMarkers(lint::LintEngine &engine, const std::string &format);

/** `--explain RULE`: one marker, by ID, name or old ID; 2 when there is none. */
int explainRule(lint::LintEngine &engine, const std::string &name, const std::string &format);

/**
 * `--check-rules`: the rule files' diagnostics (already printed) and each
 * rule's examples. 0 all pass; 1 a rule-file error or a failed example; 2
 * nothing to check.
 */
int checkRules(lint::LintEngine &engine, const LintOptions &opts, bool ruleErrors, bool none);

/** `--rules-for FILE...`: the rules each file gets; `selJson` places the files. */
int rulesFor(lint::LintEngine &engine, const std::string &selJson, const std::string &format);

}
}
