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
 *   has a selecting label (on an axis, when one is given), as `applies_to`
 *   finds it (Context::carrierAt). A label only convention gives makes a
 *   unit, with it in `conventionLabels`, but is never a node's context.
 * - Disagreements between sources are kept on their carrier's unit
 *   (`disagreements`), for a consistency rule.
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

/**
 * A context the configuration declares (sv-contexts.md §3.1; svcheck-plan
 * P2.4): for the packages, definitions and `$unit` classes in files that
 * match `files`, or the packages whose names match `packages` (globs).
 */
struct ConfigContext {
    std::vector<std::string>    files;      // relative to baseDir
    std::vector<std::string>    packages;
    std::string                 label;
    std::string                 unit;
    std::string                 axis;       // "" for the classifiers' axis of the label
    std::string                 where;      // the entry, for evidence
};

class SvContext : public lint::run::Context {
public:
    explicit SvContext(sem::IDesign *design) : m_design(design) { }

    virtual std::vector<lint::run::DeclaredContext> declaredContexts() const override;

    /** The configuration's declared contexts; `baseDir` is what their globs are relative to. */
    void setConfigContexts(const std::vector<ConfigContext> &ctxs, const std::string &baseDir) {
        m_config = ctxs;
        m_configDir = baseDir;
    }

    virtual void applyContexts(const std::vector<lint::run::ContextLabel> &labels,
        const std::vector<lint::run::ContextNote> &notes) override;

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
    std::vector<ConfigContext>                                          m_config;
    std::string                                                         m_configDir;
    // Each labelled carrier's unit; m_selects only those with a label that
    // selects (not convention alone).
    std::unordered_map<const sem::ISemNode *, sem::IUnit *>              m_unitOf;
    std::unordered_map<const sem::ISemNode *, sem::IUnit *>              m_selects;
    mutable std::unordered_map<const sem::IClass *, std::vector<sem::IClass *>> m_subclasses;
    mutable bool                                                        m_subclassesBuilt = false;
};

}
}
