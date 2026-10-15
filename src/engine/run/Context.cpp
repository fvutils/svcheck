/*
 * Context.cpp
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
#include <chrono>
#include "Context.h"
#include "Walker.h"
#include "../util/Paths.h"

namespace svcheck {
namespace lint {
namespace run {

Context::Context() { }

Context::~Context() { }

Context::IndexTimer::IndexTimer(const Context &c) : m_c(c), m_outer(!c.m_timing), m_t0(0) {
    if (m_outer) {
        m_c.m_timing = true;
        m_t0 = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
}

Context::IndexTimer::~IndexTimer() {
    if (m_outer) {
        uint64_t t1 = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        m_c.m_indexNs += t1 - m_t0;
        m_c.m_timing = false;
    }
}

void Context::indexTree(const rf::Node &root, bool owned) const {
    if (!root) {
        return;
    }
    IndexTimer timer(*this);
    if (owned && m_owned.insert(root.ptr).second) {
        m_roots.push_back(root);
    }
    if (m_parent.count(root.ptr)) {
        return;
    }
    m_parent.emplace(root.ptr, rf::Node());
    // Iterative: a deep expression must not overflow the (WASM) stack.
    std::vector<rf::Node> work{root}, kids;
    while (!work.empty()) {
        rf::Node n = work.back();
        work.pop_back();
        kids.clear();
        Walker::children(n, kids);
        for (const rf::Node &k : kids) {
            if (k.kind <= rf::NumClasses && m_parent.emplace(k.ptr, n)) {
                work.push_back(k);
            }
        }
    }
}

rf::Node Context::parentOf(const rf::Node &n) const {
    const rf::Node *p = m_parent.find(n.ptr);
    if (!p && !m_more) {
        m_more = true;
        if (indexMore()) {
            p = m_parent.find(n.ptr);
        }
    }
    return p ? *p : rf::Node();
}

rf::Node Context::rootOf(const rf::Node &n) const {
    if (!n) {
        return rf::Node();
    }
    if (!indexed(n)) {
        if (m_more || (m_more = true, !indexMore()) || !indexed(n)) {
            return rf::Node();
        }
    }
    rf::Node r = n;
    for (rf::Node p=parentOf(r); p; p=parentOf(p)) {
        r = p;
    }
    return r;
}

bool Context::owned(const rf::Node &n) const {
    rf::Node r = rootOf(n);
    return r && m_owned.count(r.ptr);
}

int32_t Context::fileidOf(const rf::Node &n) const {
    rf::Node r = rootOf(n);
    return r ? rootFileid(r) : -2;
}

bool Context::within(const rf::Node &n, const rf::Node &anc) const {
    for (rf::Node p=n; p; p=parentOf(p)) {
        if (p.ptr == anc.ptr) {
            return true;
        }
    }
    return false;
}

void Context::setFiles(const std::unordered_map<int32_t, std::string> &names,
        const std::string &baseDir) {
    m_filePath.clear();
    m_pathFile.clear();
    for (const auto &e : names) {
        std::string p = util::normalPath(e.second, baseDir);
        m_filePath[e.first] = p;
        m_pathFile.emplace(p, e.first);
    }
}

bool Context::filePath(int32_t f, std::string &path) const {
    auto it = m_filePath.find(f);
    if (it == m_filePath.end()) {
        return false;
    }
    path = it->second;
    return true;
}

int32_t Context::fileidOfPath(const std::string &path) const {
    auto it = m_pathFile.find(path);
    return (it == m_pathFile.end()) ? -2 : it->second;
}

int32_t Context::rootFileid(const rf::Node &root) const {
    int32_t fileid, line, col;
    return Walker::location(root, fileid, line, col) ? fileid : -2;
}

bool Context::declares(const rf::Node &) const {
    return false;
}

bool Context::namedPath(const rf::Node &, std::string &) const {
    return false;
}

std::string Context::spelledType(const rf::Node &) const {
    return std::string();
}

bool Context::indexMore() const {
    return false;
}

void Context::noteUnknownType(const std::string &path) const {
    if (m_unknownSeen.emplace(m_curRule, path).second) {
        m_unknown.push_back({m_curRule, m_curNode, path});
    }
}

}
}
}
