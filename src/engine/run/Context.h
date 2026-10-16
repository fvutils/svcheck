/*
 * Context.h
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
 * The run's context (svcheck-plan P1.1; pssparser C9.1, C9.2): what the
 * runner and the host's function library share during a run. It is the
 * language-neutral half of pssparser's lib::Context (lib/PssLib.h):
 *
 * - Parents: index() records every node's parent under a root before any
 *   rule runs there; a root has none.
 * - Ownership: the roots index()ed are the files the run owns.
 * - Files: the host's name for each fileid, as file_of() gives it.
 * - Unknown type names a rule asked for (the SVCHK223 notes), and the time
 *   spent building indexes.
 *
 * What a node means is the host's: it derives from Context, overrides the
 * hooks below, and registers its functions with the engine's registry
 * (cel::registerHostLib), which reach the Context through CallArgs::host.
 */
#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "../cel/CelFunctions.h"
#include "../util/PtrMap.h"
#include "Reflect.h"

namespace svcheck {
namespace lint {
namespace run {

namespace rf = svcheck::ast::reflect;

/**
 * A context label on a carrier (sv-contexts.md §2; svcheck-plan P1.4), as
 * classify() (Classify.h) resolves it on one axis.
 */
struct ContextLabel {
    enum class Source { Declared, Content, Convention, Nested };

    rf::Node        carrier;
    std::string     axis;
    std::string     label;
    std::string     unit;           // the unit's name; "" for none
    Source          source = Source::Content;
    std::string     evidence;       // where it came from: "file:line", the tag's position
};

/** A context tag written in the source: `// svcheck context: agent(apb)`. */
struct DeclaredContext {
    rf::Node        carrier;
    std::string     label;
    std::string     unit;
    std::string     where;          // "file:line"
};

class Context : public cel::Host {
public:
    Context();
    virtual ~Context();

    /**
     * Records the parents of the nodes under `root`, a file the run owns;
     * again for the same root does nothing.
     */
    void index(const rf::Node &root) { indexTree(root, true); }

    /** Whether `n` is under an indexed root (a root is). */
    bool indexed(const rf::Node &n) const { return m_parent.count(n.ptr) != 0; }

    /** The node's parent: null for a root, or a node in no indexed tree. */
    rf::Node parentOf(const rf::Node &n) const;

    /** The root (file) a node is in, or null if it is in none known. */
    rf::Node rootOf(const rf::Node &n) const;

    /** Whether the node is in one of the files the run owns. */
    bool owned(const rf::Node &n) const;

    /** The fileid of the file the node is in; -2 when it is in none known. */
    int32_t fileidOf(const rf::Node &n) const;

    /** Whether `n` is `anc` or under it. */
    bool within(const rf::Node &n, const rf::Node &anc) const;

    /** The owned roots, in the order indexed. */
    const std::vector<rf::Node> &roots() const { return m_roots; }

    /**
     * The host's name for each file, by fileid, and the directory those
     * names are made relative to ("" for none). file_of() gives the name
     * normalized ('/' separators, no '.' or '..' steps) and, under a base
     * directory, relative to it.
     */
    void setFiles(const std::unordered_map<int32_t, std::string> &names,
        const std::string &baseDir);

    /** The path file_of() gives for fileid `f`; false when the host did not name it. */
    bool filePath(int32_t f, std::string &path) const;

    /** Every file's path, by fileid, as setFiles() made them. */
    const std::unordered_map<int32_t, std::string> &filePaths() const { return m_filePath; }

    /** The fileid whose filePath() is `path`; -2 when none is. */
    int32_t fileidOfPath(const std::string &path) const;

    //* Hooks for the host. The defaults know nothing of the language.

    /**
     * The fileid of a root. Default: the root's location (Walker::location),
     * else -2.
     */
    virtual int32_t rootFileid(const rf::Node &root) const;

    /** Whether the node declares something, so has a path of its own. */
    virtual bool declares(const rf::Node &n) const;

    /**
     * The node's path, when the path names the node itself; false
     * otherwise. A message shows a declaration by it (format §3.4).
     */
    virtual bool namedPath(const rf::Node &n, std::string &path) const;

    /** A type reference as written, or "" for another node (format §3.4). */
    virtual std::string spelledType(const rf::Node &n) const;

    /**
     * The context tags written before the carriers' declarations, which
     * classify() takes as declared labels. Default: none.
     */
    virtual std::vector<DeclaredContext> declaredContexts() const;

    /**
     * The context labels found so far, after each axis classify() resolves:
     * the host makes its units of them, which the next axis's classifiers
     * and the rules read. Default: nothing.
     */
    virtual void applyContexts(const std::vector<ContextLabel> &labels);

    /**
     * Called when a node is asked about that no index()ed tree holds: the
     * host may index the trees it does not own (a library) and return true
     * to have the lookup retried. Default: false.
     */
    virtual bool indexMore() const;

    /** A type name a rule asked for that the model does not have. */
    struct UnknownType {
        const void      *rule;
        rf::Node        node;
        std::string     path;
    };

    /** What the runner is evaluating, recorded with an unknown type name. */
    void setCurrent(const void *rule, const rf::Node &node) const {
        m_curRule = rule;
        m_curNode = node;
    }

    /** Records `path` as unknown to the current rule, once per (rule, path). */
    void noteUnknownType(const std::string &path) const;

    /** Each (rule, unknown type name), once, in the order first asked. */
    const std::vector<UnknownType> &unknownTypes() const { return m_unknown; }

    uint64_t numIndexed() const { return m_parent.size(); }

    /**
     * Time spent building indexes (pssparser C8): the owned files' parent
     * links and whatever the host builds under an IndexTimer. The runner
     * charges it to the run, not to the rule that first asked.
     */
    uint64_t indexNs() const { return m_indexNs; }

protected:
    // Times an index build into m_indexNs; a nested one counts once.
    class IndexTimer {
    public:
        IndexTimer(const Context &c);
        ~IndexTimer();
    private:
        const Context   &m_c;
        bool            m_outer;
        uint64_t        m_t0;
    };

    /** Records the parents under `root`; `owned` adds it to the run's files. */
    void indexTree(const rf::Node &root, bool owned) const;

private:
    // Filled on demand from const queries: the context is the evaluator's
    // read-only Host, but what it knows grows as rules ask.
    mutable util::PtrMap<rf::Node>                              m_parent;
    mutable std::unordered_set<const void *>                    m_owned;     // roots
    mutable std::vector<rf::Node>                               m_roots;     // owned, in order
    mutable bool                                                m_more = false;  // indexMore() done
    std::unordered_map<int32_t, std::string>                    m_filePath;  // fileid -> path
    std::unordered_map<std::string, int32_t>                    m_pathFile;  // path -> fileid
    mutable const void                                          *m_curRule = nullptr;
    mutable rf::Node                                            m_curNode;
    mutable std::vector<UnknownType>                            m_unknown;
    mutable std::set<std::pair<const void *, std::string>>      m_unknownSeen;
    mutable uint64_t                                            m_indexNs = 0;
    mutable bool                                                m_timing = false;
};

}
}
}
