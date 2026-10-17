/*
 * Config.h
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
 * `svcheck --config FILE` (svcheck-plan P2.3; provisional: P2.9 adds TOML
 * and discovery onto the same tables). The file is YAML or JSON:
 *
 *   svcheck:
 *     rules: [path...]                 rule files or directories, as --rules
 *     profile: recommended
 *     select: [selector...]
 *     ignore: [selector...]
 *     rules-version: "2026.1"
 *     rule: {<ID or name>: {<option>: value}}
 *     severity: {<selector>: error | warning | info | off}
 *     warnings: {error: true | [selector...], no-error: [...], none: bool}
 *     overrides:                       in order; a later one wins
 *       - files: [glob...]             relative to the file's directory
 *         context: agent               or [labels], "agent(apb)" for one unit
 *         profile, select, ignore, rule
 *     contexts:                        declared contexts (sv-contexts.md §3.1)
 *       - files: [glob...]             the carriers declared in these files
 *         packages: [glob...]          or these packages (by name)
 *         context: vip                 "label" or "label(unit)"
 *         axis: provenance             the label's axis
 *
 * Paths and globs are relative to the file's directory.
 */
#pragma once
#include <string>
#include <vector>
#include "yaml/YamlDoc.h"

namespace svcheck {
namespace lint {
class JsonWriter;
}
namespace cli {

/** A context the configuration declares for some carriers (P2.4). */
struct DeclaredContextRule {
    std::vector<std::string>    files;
    std::vector<std::string>    packages;
    std::string                 label;
    std::string                 unit;
    std::string                 axis;
};

struct Config {
    std::string                         path;       // "" for none
    std::string                         dir;        // its directory, absolute
    lint::yaml::NodeUP                  doc;
    const lint::yaml::Node              *root = nullptr;    // the `svcheck:` mapping
    std::vector<std::string>            rules;      // absolute
    std::vector<DeclaredContextRule>    contexts;
};

/** Reads `path` into `out`; false with `err` (one line per problem) when it is not valid. */
bool readConfig(const std::string &path, Config &out, std::string &err);

/** A YAML node as JSON. */
void writeJson(lint::JsonWriter &w, const lint::yaml::Node *n);

}
}
