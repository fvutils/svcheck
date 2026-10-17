/*
 * SlangWarnings.cpp
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
 */
#include <cstdio>
#include <map>
#include "SlangWarnings.h"
#include "util/JsonWriter.h"

namespace svcheck {
namespace host {

std::string slangMarkerId(int num) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "SLANG%03d", num);
    return buf;
}

const SlangWarningInfo *slangWarningByName(const std::string &option) {
    static const std::map<std::string, const SlangWarningInfo *> byName = [] {
        std::map<std::string, const SlangWarningInfo *> m;
        for (const SlangWarningInfo *w = slangWarnings(); w->name; w++) {
            m.emplace(w->name, w);
        }
        return m;
    }();
    auto it = byName.find(option);
    return (it == byName.end()) ? nullptr : it->second;
}

std::string slangHostMarkersJson() {
    lint::JsonWriter w;
    w.beginObject().key("markers").beginArray();
    for (const SlangWarningInfo *s = slangWarnings(); s->name; s++) {
        w.beginObject().kv("id", slangMarkerId(s->num)).kv("name", std::string(s->name));
        w.kv("tier", s->inDefault ? "suspicious" : "pedantic").kv("family", "slang");
        w.kv("status", s->removed ? "retired" : "active").kv("default_enabled", true);
        w.endObject();
    }
    w.endArray().endObject();
    return w.str();
}

std::vector<std::string> slangWarningOptions(const std::set<std::string> &ids) {
    std::vector<std::string> out;
    for (const SlangWarningInfo *s = slangWarnings(); s->name; s++) {
        if (!s->inDefault && !s->removed && ids.count(slangMarkerId(s->num))) {
            out.push_back(s->name);
        }
    }
    return out;
}

}
}
