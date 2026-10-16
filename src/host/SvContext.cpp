/*
 * SvContext.cpp
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
 */
#include <algorithm>
#include <map>
#include "SvContext.h"
#include "Unit.h"
#include "UnitDep.h"
#include "reflect/Reflect.h"
#include "run/Classify.h"
#include "svcheck/sem/IClass.h"
#include "svcheck/sem/IDefinition.h"
#include "svcheck/sem/IPackage.h"

namespace svcheck {
namespace host {

namespace {

namespace rf = svcheck::ast::reflect;
using lint::run::ContextLabel;
using lint::run::DeclaredContext;

// Every declaration that can carry a tag: packages, definitions, and the
// classes in them and in $unit, nested ones too.
void carriersIn(const std::vector<sem::IDeclUP> &items, std::vector<sem::IDecl *> &out) {
    for (const sem::IDeclUP &d : items) {
        if (auto *c = dynamic_cast<sem::IClass *>(d.get())) {
            out.push_back(c);
            carriersIn(c->getMembers(), out);
        }
    }
}

}

std::vector<DeclaredContext> SvContext::declaredContexts() const {
    std::vector<sem::IDecl *> decls;
    for (const sem::IPackageUP &p : m_design->getPackages()) {
        decls.push_back(p.get());
        carriersIn(p->getItems(), decls);
    }
    for (const sem::IDefinitionUP &d : m_design->getDefinitions()) {
        decls.push_back(d.get());
        carriersIn(d->getItems(), decls);
    }
    carriersIn(m_design->getItems(), decls);

    std::vector<DeclaredContext> out;
    for (sem::IDecl *d : decls) {
        for (const std::string &tag : d->getTags()) {
            DeclaredContext dc;
            dc.carrier = sem::reflect::classify(d);
            size_t open = tag.find('(');
            size_t close = tag.rfind(')');
            if (open != std::string::npos && close != std::string::npos && close > open) {
                dc.label = tag.substr(0, open);
                dc.unit = tag.substr(open + 1, close - open - 1);
            } else {
                dc.label = tag;
            }
            std::string path;
            const sem::Location &loc = d->getLocation();
            dc.where = (filePath(loc.fileid, path) ? path : std::string("?")) + ":"
                + std::to_string(loc.lineno);
            out.push_back(dc);
        }
    }
    return out;
}

void SvContext::applyContexts(const std::vector<ContextLabel> &labels) {
    m_design->getUnits().clear();
    m_design->getDeps().clear();
    m_unitOf.clear();
    m_axes.clear();

    // Carriers with one unit name are one unit; the name is the label's, else
    // the carrier's qualified name.
    std::map<std::string, sem::Unit *> byName;
    for (const ContextLabel &l : labels) {
        sem::ISemNode *node = sem::reflect::asSemNode(l.carrier);
        auto *decl = dynamic_cast<sem::IDecl *>(node);
        if (!decl) {
            continue;
        }
        std::string key = l.unit.empty() ? decl->getQname() : l.unit;
        sem::Unit *&u = byName[key];
        if (!u) {
            u = new sem::Unit();
            u->setName(key);
            m_design->getUnits().push_back(sem::IUnitUP(u));
        }
        std::vector<sem::IDecl *> &carriers = u->getCarriers();
        if (std::find(carriers.begin(), carriers.end(), decl) == carriers.end()) {
            // A unit is where its first carrier is, so a finding on it has a place.
            if (carriers.empty()) {
                u->setLocation(decl->getLocation());
            }
            carriers.push_back(decl);
        }
        std::vector<std::string> &ls = u->getLabels();
        if (std::find(ls.begin(), ls.end(), l.label) == ls.end()) {
            ls.push_back(l.label);
        }
        u->getEvidence().push_back(l.axis + " " + l.label + ": "
            + lint::run::sourceName(l.source) + " (" + l.evidence + ") on "
            + rf::classes[l.carrier.kind].name + " " + decl->getName());
        // A carrier in two units (two labels, two names) answers with the first.
        m_unitOf.emplace(node, u);
        m_axes[node].insert(l.axis);
    }

    // Import edges between units, each once.
    std::set<std::pair<const sem::IUnit *, const sem::IUnit *>> seen;
    for (const sem::IUnitUP &u : m_design->getUnits()) {
        for (sem::IDecl *c : u->getCarriers()) {
            std::vector<sem::IPackage *> none;
            const std::vector<sem::IPackage *> *imports = &none;
            if (auto *p = dynamic_cast<sem::IPackage *>(c)) {
                imports = &p->getImports();
            } else if (auto *d = dynamic_cast<sem::IDefinition *>(c)) {
                imports = &d->getImports();
            }
            for (sem::IPackage *p : *imports) {
                auto it = m_unitOf.find(static_cast<sem::ISemNode *>(p));
                if (it == m_unitOf.end() || it->second == u.get()
                        || !seen.insert({u.get(), it->second}).second) {
                    continue;
                }
                sem::UnitDep *e = new sem::UnitDep();
                e->setKind(sem::UnitDepKind::Import);
                e->setFrom(u.get());
                e->setLocation(c->getLocation());   // the importing carrier
                e->setTo(it->second);
                m_design->getDeps().push_back(sem::IUnitDepUP(e));
            }
        }
    }
}

const std::vector<sem::IClass *> &SvContext::subclassesOf(const sem::IClass *c) const {
    if (!m_subclassesBuilt) {
        std::vector<sem::IDecl *> decls;
        for (const sem::IPackageUP &p : m_design->getPackages()) {
            carriersIn(p->getItems(), decls);
        }
        for (const sem::IDefinitionUP &d : m_design->getDefinitions()) {
            carriersIn(d->getItems(), decls);
        }
        carriersIn(m_design->getItems(), decls);
        for (sem::IDecl *d : decls) {
            auto *k = dynamic_cast<sem::IClass *>(d);
            if (k && k->getBase()) {
                m_subclasses[k->getBase()].push_back(k);
            }
        }
        m_subclassesBuilt = true;
    }
    static const std::vector<sem::IClass *> none;
    auto it = m_subclasses.find(c);
    return it == m_subclasses.end() ? none : it->second;
}

sem::IUnit *SvContext::contextOf(const rf::Node &n, const std::string &axis) const {
    for (rf::Node p=n; p; p=parentOf(p)) {
        const sem::ISemNode *s = sem::reflect::asSemNode(p);
        auto it = m_unitOf.find(s);
        if (it == m_unitOf.end()) {
            continue;
        }
        if (axis.empty()) {
            return it->second;
        }
        auto a = m_axes.find(s);
        if (a != m_axes.end() && a->second.count(axis)) {
            return it->second;
        }
    }
    return nullptr;
}

}
}
