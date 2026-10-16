/*
 * SvContext.h
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
 * The run's context over the curated model (svcheck-plan P1.4): the host's
 * half of the classify stage (sv-contexts.md §5.2).
 *
 * - Declared tags: each `// svcheck context:` tag the lowering found on a
 *   package, definition or class (Decl.tags): `agent(apb)` is label `agent`,
 *   unit `apb`.
 * - Units: after each axis, the labelled carriers are grouped into Units by
 *   unit name, in Design.units, and a unit's carriers' package imports of
 *   another unit's carriers are UnitDep edges (`Import`), in Design.deps.
 * - `context(n)`: the unit of the innermost carrier, `n` or above it, that
 *   has one (on an axis, when one is given).
 */
#pragma once
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include "run/Context.h"
#include "svcheck/sem/IClass.h"
#include "svcheck/sem/IDesign.h"
#include "svcheck/sem/IUnit.h"

namespace svcheck {
namespace host {

class SvContext : public lint::run::Context {
public:
    explicit SvContext(sem::IDesign *design) : m_design(design) { }

    virtual std::vector<lint::run::DeclaredContext> declaredContexts() const override;

    virtual void applyContexts(const std::vector<lint::run::ContextLabel> &labels) override;

    /**
     * The unit of the innermost carrier at or above `n` that has one, with a
     * label on `axis` when it is not empty; null for none.
     */
    sem::IUnit *contextOf(const ast::reflect::Node &n, const std::string &axis) const;

    /**
     * The classes whose base is `c`, in declaration order (user and library
     * code). The index is built on the first call.
     */
    const std::vector<sem::IClass *> &subclassesOf(const sem::IClass *c) const;

    /** The model, whose units and dependencies applyContexts() made. */
    sem::IDesign *design() const { return m_design; }

private:
    sem::IDesign                                                        *m_design;
    std::unordered_map<const sem::ISemNode *, sem::IUnit *>              m_unitOf;
    // The axes each carrier has a label on: the model's Unit keeps labels only.
    std::unordered_map<const sem::ISemNode *, std::set<std::string>>     m_axes;
    mutable std::unordered_map<const sem::IClass *, std::vector<sem::IClass *>> m_subclasses;
    mutable bool                                                        m_subclassesBuilt = false;
};

}
}
