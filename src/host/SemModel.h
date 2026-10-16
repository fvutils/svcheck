/*
 * SemModel.h
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
 * The curated model as a semantic layer (Schema.h, SemanticLayer::Model):
 * its generated tables (src/ast) and the location hooks over them, and
 * lowering a compilation for the run (src/lower).
 */
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "Reflect.h"
#include "svcheck/sem/IDesign.h"

namespace slang {
namespace ast {
class Compilation;
}
}

namespace svcheck {
namespace host {

class Sources;
namespace rf = svcheck::ast::reflect;

/** The curated model's tables and hooks, with its own ids (Design is one of 1..N). */
const rf::Schema &semModelSchema();

/** The node for a model node; a null node for nullptr. */
rf::Node semNode(sem::ISemNode *n);

/**
 * Lowers an elaborated compilation. Locations go through `sources`; a
 * symbol is library code when its file is under one of `libraryDirs`.
 */
std::unique_ptr<sem::IDesign> lowerCompilation(slang::ast::Compilation &c, Sources &sources,
    const std::vector<std::string> &libraryDirs);

}
}
