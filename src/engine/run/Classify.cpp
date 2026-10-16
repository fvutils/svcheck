/*
 * Classify.cpp
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
#include <set>
#include "Classify.h"
#include "Walker.h"
#include "../cel/CelEval.h"
#include "../cel/CelNode.h"
#include "../rules/Rule.h"

namespace svcheck {
namespace lint {
namespace run {

namespace {

cel::Value nodeValue(const rf::Node &n) {
    cel::NodeRef r;
    r.kind = n.kind;
    r.ptr = n.ptr;
    return cel::Value::mkNode(r);
}

// A node's `name` field, when its class has a string one; else "".
std::string nameOf(const rf::Node &n) {
    for (uint16_t c=n.kind; c; c=rf::classes[c].super) {
        const rf::ClassInfo &ci = rf::classes[c];
        for (uint16_t f=ci.firstField; f<ci.firstField + ci.numFields; f++) {
            const rf::FieldInfo &fi = rf::fields[f];
            if (fi.type == rf::FieldType::String && std::string(fi.name) == "name") {
                rf::Value v = fi.get(n.ptr, n.kind);
                return (v.kind == rf::Value::Kind::String) ? std::string(v.s.data, v.s.size) : "";
            }
        }
    }
    return "";
}

bool matches(const rules::KindSet &k, uint16_t kind) {
    for (uint16_t c : k.classes) {
        if (cel::isSubclass(kind, c)) {
            return true;
        }
    }
    return false;
}

std::string where(const rules::Classifier &c) {
    return c.file + ":" + std::to_string(c.mark.line);
}

// What one classifier found on one carrier.
struct Found {
    std::string     label;
    std::string     unit;
    bool            strong;
    std::string     evidence;
};

}

const char *sourceName(ContextLabel::Source s) {
    switch (s) {
    case ContextLabel::Source::Declared: return "Declared";
    case ContextLabel::Source::Content: return "Content";
    case ContextLabel::Source::Convention: return "Convention";
    case ContextLabel::Source::Nested: return "Nested";
    }
    return "?";
}

Contexts classify(const std::vector<const rules::Classifier *> &classifiers, Context &ctx,
        uint64_t maxSteps) {
    Contexts out;
    std::vector<std::string> axes;
    std::map<std::string, std::string> axisOf;      // label -> axis
    for (const rules::Classifier *c : classifiers) {
        if (std::find(axes.begin(), axes.end(), c->axis) == axes.end()) {
            axes.push_back(c->axis);
        }
        axisOf.emplace(c->label, c->axis);
    }

    // The carriers: every node of a kind some classifier names, in walk order.
    std::vector<rf::Node> carriers;
    std::set<const void *> isCarrier;
    for (const rf::Node &root : ctx.roots()) {
        std::vector<rf::Node> work = {root}, kids;
        while (!work.empty()) {
            rf::Node n = work.back();
            work.pop_back();
            for (const rules::Classifier *c : classifiers) {
                if (matches(c->carrier, n.kind)) {
                    carriers.push_back(n);
                    isCarrier.insert(n.ptr);
                    break;
                }
            }
            kids.clear();
            Walker::children(n, kids);
            work.insert(work.end(), kids.rbegin(), kids.rend());
        }
    }

    std::vector<DeclaredContext> declared = ctx.declaredContexts();
    for (const DeclaredContext &d : declared) {
        if (!axisOf.count(d.label)) {
            out.notes.push_back({d.carrier, d.where + ": context tag '" + d.label + "' is a "
                "label no loaded classifier has, so it has no axis; it is ignored"});
        }
    }

    for (const std::string &axis : axes) {
        std::map<const void *, std::vector<Found>> found;
        for (const rules::Classifier *c : classifiers) {
            if (c->axis != axis) {
                continue;
            }
            bool failed = false;
            for (const rf::Node &n : carriers) {
                if (failed || !matches(c->carrier, n.kind)) {
                    continue;
                }
                std::vector<std::pair<std::string, cel::Value>> vars;
                vars.emplace_back("carrier", nodeValue(n));
                for (const rules::RuleOption &o : c->options) {
                    vars.emplace_back("options." + o.name, o.def);
                }
                auto eval = [&](const cel::Expr *e, const char *field, cel::Value &v) {
                    cel::EvalOptions opts;
                    opts.maxSteps = maxSteps;
                    opts.host = &ctx;
                    cel::CelEval ev(opts);
                    v = ev.eval(e, vars);
                    if (v.isError() || ev.budgetExceeded()) {
                        out.notes.push_back({n, "classifier '" + c->label + "' (" + where(*c)
                            + ") failed in its " + field + ": " + (ev.budgetExceeded()
                            ? std::string("it ran out of steps") : v.s)
                            + "; it is skipped for the rest of the run"});
                        failed = true;
                        return false;
                    }
                    return true;
                };
                cel::Value v;
                if (!eval(c->when.get(), "when", v) || v.kind != cel::Value::Kind::Bool || !v.b) {
                    continue;
                }
                std::string unit = nameOf(n);
                if (c->name) {
                    if (!eval(c->name.get(), "name", v)) {
                        continue;
                    }
                    unit = v.s;
                }
                found[n.ptr].push_back({c->label, unit, c->strong, where(*c)});
                if (c->joins) {
                    if (!eval(c->joins.get(), "joins", v)) {
                        continue;
                    }
                    if (v.kind != cel::Value::Kind::List || !v.list) {
                        continue;
                    }
                    for (const cel::Value &j : *v.list) {
                        if (j.kind == cel::Value::Kind::Node && isCarrier.count(j.node.ptr)) {
                            found[j.node.ptr].push_back({c->label, unit, c->strong,
                                where(*c) + " (joined)"});
                        }
                    }
                }
            }
        }

        // Resolve each carrier's labels on this axis, in walk order.
        std::map<const void *, std::vector<ContextLabel>> mine;
        for (const rf::Node &n : carriers) {
            std::vector<ContextLabel> &res = mine[n.ptr];
            auto add = [&res, &n, &axis](const std::string &label, const std::string &unit,
                    ContextLabel::Source src, const std::string &ev) {
                for (const ContextLabel &l : res) {
                    if (l.label == label) {
                        return;
                    }
                }
                ContextLabel l;
                l.carrier = n;
                l.axis = axis;
                l.label = label;
                l.unit = unit;
                l.source = src;
                l.evidence = ev;
                res.push_back(l);
            };
            for (const DeclaredContext &d : declared) {
                auto a = axisOf.find(d.label);
                if (d.carrier.ptr == n.ptr && a != axisOf.end() && a->second == axis) {
                    add(d.label, d.unit.empty() ? nameOf(n) : d.unit,
                        ContextLabel::Source::Declared, d.where);
                }
            }
            const std::vector<Found> &f = found[n.ptr];
            bool anyStrong = std::any_of(f.begin(), f.end(), [](const Found &x) {
                return x.strong; });
            bool tagged = !res.empty();
            for (const Found &x : f) {
                if (x.strong && !tagged) {
                    add(x.label, x.unit, ContextLabel::Source::Content, x.evidence);
                } else if (!x.strong && !anyStrong && !tagged) {
                    add(x.label, x.unit, ContextLabel::Source::Convention, x.evidence);
                }
            }
            // A weaker source that disagrees with the one used (Q-CX4's data).
            for (const Found &x : f) {
                bool used = std::any_of(res.begin(), res.end(), [&x](const ContextLabel &l) {
                    return l.label == x.label; });
                if (!used && (x.strong || anyStrong || tagged)) {
                    out.notes.push_back({n, std::string(x.strong ? "content" : "convention")
                        + " (" + x.evidence + ") says '" + x.label + "', but " + (tagged
                        ? "the tag" : "content") + " says " + [&res]() {
                            std::string s;
                            for (const ContextLabel &l : res) {
                                s += (s.empty() ? "'" : ", '") + l.label + "'";
                            }
                            return s;
                        }() + " on axis '" + axis + "'"});
                }
            }
        }

        // Nested carriers sum (Q-CX3): an outer carrier has the inner ones'
        // labels. Inner carriers come later in walk order, so they are done
        // first; each gives its labels to its nearest outer carrier, which
        // takes them in walk order once its own inner ones are all done.
        std::map<const void *, std::vector<std::vector<ContextLabel>>> fromInner;
        for (auto it=carriers.rbegin(); it!=carriers.rend(); it++) {
            std::vector<ContextLabel> &own = mine[it->ptr];
            auto &inner = fromInner[it->ptr];
            for (auto c=inner.rbegin(); c!=inner.rend(); c++) {
                for (const ContextLabel &l : *c) {
                    bool has = std::any_of(own.begin(), own.end(),
                        [&l](const ContextLabel &o) { return o.label == l.label; });
                    if (!has) {
                        // The outer carrier keeps its own name: it is not
                        // joined to the inner one's unit.
                        ContextLabel n = l;
                        n.carrier = *it;
                        n.unit = nameOf(*it);
                        n.source = ContextLabel::Source::Nested;
                        own.push_back(n);
                    }
                }
            }
            for (rf::Node p=ctx.parentOf(*it); p; p=ctx.parentOf(p)) {
                if (isCarrier.count(p.ptr)) {
                    fromInner[p.ptr].push_back(own);
                    break;
                }
            }
        }

        for (const rf::Node &n : carriers) {
            for (const ContextLabel &l : mine[n.ptr]) {
                out.labels.push_back(l);
            }
        }
        ctx.applyContexts(out.labels);
    }
    return out;
}

}
}
}
