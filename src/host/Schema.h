/*
 * Schema.h
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
 * The schema the SV host installs (svcheck-plan P1.3): one semantic layer,
 * then the syntax schema (Syntax.h).
 *
 * The engine reads one schema, so the layers are merged into one set of
 * tables: the semantic layer's classes keep their ids (1..N), and the syntax
 * schema's follow, its class, field, enum and struct indexes shifted past
 * the semantic layer's. A class name must be unique across both; install
 * fails if one is not.
 *
 * Which semantic layer (Q-SC5, decided at P1.8):
 * - Model: the curated model (svcheck-design §3.6; src/ast, src/lower);
 * - Slang: a thin accessor table over slang's own AST (SlangSchema.h);
 * - None: the syntax schema alone.
 */
#pragma once
#include <string>
#include "Reflect.h"

namespace svcheck {
namespace host {

enum class SemanticLayer { None, Model, Slang };

/** "none", "model", "slang"; false for another name. */
bool semanticLayerOf(const std::string &name, SemanticLayer &layer);

/**
 * Installs the merged schema. Call once, before any rule is loaded
 * (Reflect.h install()); a later call does nothing. Aborts, naming the
 * class, when the two layers share a class name.
 */
void installSchema(SemanticLayer layer);

/** The layer installed; None before installSchema(). */
SemanticLayer installedLayer();

/** Whether class `kind` of the merged schema is the semantic layer's. */
bool isSemanticClass(uint16_t kind);

}
}
