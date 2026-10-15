/*
 * Marker.h
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
 * The concrete marker (pssparser src/Marker.h, made header-only).
 */
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "svcheck/IMarker.h"

namespace svcheck {

class Marker : public IMarker {
public:
    Marker(const std::string &msg, MarkerSeverityE severity, const ast::Location &loc,
            const std::string &id="") :
        m_msg(msg), m_severity(severity), m_loc(loc), m_id(id) { }

    virtual ~Marker() { }

    virtual const std::string &msg() const override { return m_msg; }
    virtual void setMsg(const std::string &m) override { m_msg = m; }
    virtual MarkerSeverityE severity() const override { return m_severity; }
    virtual void setSeverity(MarkerSeverityE s) override { m_severity = s; }
    virtual const ast::Location &loc() const override { return m_loc; }
    virtual void setLocation(const ast::Location &l) override { m_loc = l; }
    virtual const std::string &id() const override { return m_id; }
    virtual void setId(const std::string &id) override { m_id = id; }

    virtual const std::vector<MarkerRelation> &related() const override { return m_related; }
    virtual void addRelated(const ast::Location &loc, const std::string &label) override {
        m_related.push_back(MarkerRelation{loc, label});
    }

    virtual const std::vector<MarkerFix> &fixes() const override { return m_fixes; }
    virtual void addFix(const ast::Location &span, const std::string &replacement) override {
        m_fixes.push_back(MarkerFix{span, replacement});
    }

    virtual IMarker *clone() const override { return new Marker(*this); }

private:
    std::string                 m_msg;
    MarkerSeverityE             m_severity;
    ast::Location               m_loc;
    std::string                 m_id;
    std::vector<MarkerRelation> m_related;
    std::vector<MarkerFix>      m_fixes;
};

using MarkerUP = std::unique_ptr<Marker>;

}
