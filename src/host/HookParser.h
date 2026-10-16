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
 * How the engine's test hooks make trees of SV source text (snippet.sv).
 */
#pragma once
#include <string>
#include "LintTestHooks.h"
#include "Schema.h"

namespace svcheck {
namespace host {

/** The syntax tree of the source alone (P1.2). */
lint::HookParser syntaxHookParser();

/**
 * The source compiled and elaborated, for a semantic layer (P1.3): its
 * roots are the layer's root, then the syntax tree's. When `uvmDir` is set
 * (UVM's `src` directory) and the source mentions `uvm_`, uvm_pkg is
 * compiled with it, parsed once and kept; its code is library code. A
 * source with errors is refused, with the first one.
 */
lint::HookParser semanticHookParser(SemanticLayer layer, const std::string &uvmDir);

}
}
