/*
 * SemModel.cpp
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
#include <string>
#include <vector>
#include "Lower.h"
#include "SemModel.h"
#include "Syntax.h"
#include "reflect/Reflect.h"
#include "svcheck/sem/IDecl.h"
#include "slang/ast/Compilation.h"
#include "slang/text/SourceManager.h"

namespace svcheck {
namespace host {

namespace {

bool location(const rf::Node &n, ast::Location &loc) {
    loc = ast::Location();
    sem::ISemNode *s = sem::reflect::asSemNode(n);
    if (!s) {
        return false;
    }
    const sem::Location &l = s->getLocation();
    if (l.lineno < 1) {
        return false;
    }
    loc.fileid = l.fileid;
    loc.lineno = l.lineno;
    loc.linepos = l.linepos;
    loc.extent = l.extent;
    return true;
}

// A declaration's location is its name's already (the lowering puts it there).
bool nameLocation(const rf::Node &n, ast::Location &loc) {
    loc = ast::Location();
    sem::ISemNode *s = sem::reflect::asSemNode(n);
    return s && dynamic_cast<sem::IDecl *>(s) && location(n, loc);
}

bool startsWith(const std::string &s, const std::string &prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

}

const rf::Schema &semModelSchema() {
    static const rf::Schema s = [] {
        rf::Schema r;
        r.numClasses = sem::reflect::NumClasses;
        r.classes = sem::reflect::classes;
        r.fields = sem::reflect::fields;
        r.numFields = sem::reflect::numFields;
        r.enums = sem::reflect::enums;
        r.numEnums = sem::reflect::numEnums;
        r.structs = sem::reflect::structs;
        r.numStructs = sem::reflect::numStructs;
        r.location = &location;
        r.nameLocation = &nameLocation;
        return r;
    }();
    return s;
}

rf::Node semNode(sem::ISemNode *n) {
    return sem::reflect::classify(n);
}

std::unique_ptr<sem::IDesign> lowerCompilation(slang::ast::Compilation &c, Sources &sources,
        const std::vector<std::string> &libraryDirs) {
    lower::LowerHost host;
    host.locate = [&sources](slang::SourceRange r, sem::Location &loc) {
        ast::Location l;
        if (!sources.locate(r, l)) {
            return false;
        }
        loc.fileid = l.fileid;
        loc.lineno = l.lineno;
        loc.linepos = l.linepos;
        loc.extent = l.extent;
        return true;
    };
    host.filePath = [&sources](slang::SourceLocation loc) {
        return sources.pathOf(loc);
    };
    host.macroOf = [&sources](slang::SourceLocation loc) {
        return sources.macroOf(loc);
    };
    host.isLibrary = [&libraryDirs, fp = host.filePath](const slang::ast::Symbol &sym) {
        if (!sym.location.valid()) {
            return true;        // built in: the std package
        }
        std::string p = fp(sym.location);
        for (const std::string &d : libraryDirs) {
            if (startsWith(p, d)) {
                return true;
            }
        }
        return false;
    };
    return lower::lower(c, host);
}

}
}
