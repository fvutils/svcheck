/*
 * Lower.h
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
 * The lowering pass: slang's semantic model to the curated model
 * (svcheck-design §3.6; svcheck-plan P1.3).
 *
 * - User code is lowered in full; library code (the host says which) as
 *   declarations only: classes, members and signatures, no bodies.
 * - One Class per class declaration. A parameterised class's members come
 *   from its default specialisation, and every specialisation refers to the
 *   generic's node (S-2).
 * - Every Decl knows its carrier: the package or definition it is in. A
 *   package lists the files its text comes from, so an `include`d file is
 *   known to be part of the package it was included into (svcheck-plan R-6).
 * - What the model does not curate becomes OpaqueDecl, OpaqueStmt or
 *   OpaqueExpr, with its children still lowered.
 *
 * Nothing here knows the rule engine; the host installs the schema.
 */
#pragma once
#include <functional>
#include <memory>
#include <string>
#include "slang/text/SourceLocation.h"
#include "svcheck/sem/IDesign.h"
#include "svcheck/sem/Location.h"

namespace slang {
namespace ast {
class Compilation;
class Symbol;
}
}

namespace svcheck {
namespace lower {

/** What the lowering asks of its host. */
struct LowerHost {
    /** Where `range` starts, with macro expansions mapped to their use; false for nowhere. */
    std::function<bool(slang::SourceRange range, sem::Location &loc)>  locate;
    /** The path of the file whose text `loc` is in; "" for none. */
    std::function<std::string(slang::SourceLocation loc)>             filePath;
    /** Whether a symbol is library code, lowered as a declaration only. */
    std::function<bool(const slang::ast::Symbol &sym)>                 isLibrary;
};

/**
 * Lowers an elaborated compilation (call getAllDiagnostics() or getRoot()
 * first). The model refers to nothing in `c`: it may outlive it.
 */
std::unique_ptr<sem::IDesign> lower(slang::ast::Compilation &c, const LowerHost &host);

}
}
