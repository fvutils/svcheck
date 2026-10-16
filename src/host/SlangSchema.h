/*
 * SlangSchema.h
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
 * The other semantic layer for Q-SC5 (Schema.h, SemanticLayer::Slang): a
 * thin, hand-written accessor table over slang's own AST, just enough for
 * the 4 UVM rules of svcheck-plan P1.3, to set against the curated model.
 */
#pragma once
#include <string>
#include <vector>
#include "Reflect.h"

namespace slang {
class SourceManager;
namespace ast {
class Compilation;
}
}

namespace svcheck {
namespace host {

class Sources;
namespace rf = svcheck::ast::reflect;

/** The accessor table and its hooks, with its own ids. */
const rf::Schema &slangSchema();

/** The root node of a compilation, in the accessor table. */
rf::Node slangRoot(const slang::ast::Compilation &c);

/**
 * The run's Sources, which the location hooks read, and what `inLibrary`
 * reads: a symbol is library code when its file is under one of
 * `libraryDirs`. Null to end the run.
 */
void setSlangSources(Sources *s, const slang::SourceManager *sm=nullptr,
    const std::vector<std::string> &libraryDirs={});

/** Whether a ClassType node is, or derives from, a class named `name`. */
bool slangDerivesFrom(const rf::Node &n, const std::string &name);

}
}
