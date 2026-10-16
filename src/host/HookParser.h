/*
 * HookParser.h
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
 * How the engine's test hooks and the examples harness make trees of SV
 * source text.
 */
#pragma once
#include <string>
#include "LintTestHooks.h"
#include "Schema.h"

namespace svcheck {
namespace host {

/**
 * Compiles source files, each a compilation unit named as given.
 *
 * - With no semantic layer, each file is parsed alone (P1.2), and its roots
 *   are the syntax trees. `strict`: a file with a syntax error is refused.
 * - With one (P1.3), the files are compiled and elaborated together, and
 *   their roots are the layer's root, then the syntax trees. A file with
 *   errors is always refused. When `uvmDir` is set (UVM's `src` directory)
 *   and a file mentions `uvm_`, uvm_pkg is compiled with them, parsed once
 *   and kept; its code is library code.
 *
 * A refusal gives the first error.
 */
lint::run::Compiler hostCompiler(SemanticLayer layer, const std::string &uvmDir, bool strict);

/** The node hooks' parser: the source alone, as `snippet.sv`, through `compile`. */
lint::HookParser hookParser(lint::run::Compiler compile);

}
}
