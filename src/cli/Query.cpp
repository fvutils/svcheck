/*
 * Query.cpp
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
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include "Config.h"
#include "HookParser.h"
#include "Lint.h"
#include "Query.h"
#include "SlangWarnings.h"
#include "pipeline/Selection.h"
#include "rules/Rule.h"
#include "run/Examples.h"
#include "util/JsonWriter.h"
#include "yaml/YamlDoc.h"

namespace svcheck {
namespace cli {

namespace {

namespace fs = std::filesystem;
using NK = lint::yaml::Node::Kind;

lint::yaml::NodeUP parse(const std::string &text) {
    try {
        return lint::yaml::parse("<json>", text);
    } catch (const lint::yaml::YamlError &) {
        return nullptr;
    }
}

std::string str(const lint::yaml::Node *n, const char *key) {
    const lint::yaml::Node *v = n ? n->get(key) : nullptr;
    if (!v) {
        return "";
    }
    if (v->kind == NK::String) {
        return v->s;
    }
    if (v->kind == NK::Bool) {
        return v->b ? "true" : "false";
    }
    return v->s;
}

std::string list(const lint::yaml::Node *n, const char *key) {
    const lint::yaml::Node *v = n ? n->get(key) : nullptr;
    std::string out;
    if (v && v->kind == NK::Seq) {
        for (const lint::yaml::NodeUP &i : v->items) {
            out += (out.empty() ? "" : ", ") + i->s;
        }
    }
    return out;
}

std::string shownPath(const std::string &path) {
    std::error_code ec;
    fs::path rel = fs::path(path).lexically_relative(fs::current_path(ec));
    return (!ec && !rel.empty() && *rel.begin() != "..") ? rel.generic_string() : path;
}

// A scalar or a list as one line of text.
std::string valueText(const lint::yaml::Node *v) {
    if (!v || v->kind == NK::Null) {
        return "null";
    }
    if (v->kind == NK::String) {
        return "\"" + v->s + "\"";
    }
    if (v->kind == NK::Seq) {
        std::string s = "[";
        for (size_t i=0; i<v->items.size(); i++) {
            s += (i ? ", " : "") + valueText(v->items[i].get());
        }
        return s + "]";
    }
    return v->s;
}

}

int listMarkers(lint::LintEngine &engine, const std::string &format) {
    std::string cat = engine.catalogJson();
    if (format == "json") {
        std::printf("%s\n", cat.c_str());
        return 0;
    }
    lint::yaml::NodeUP doc = parse(cat);
    const lint::yaml::Node *ms = doc ? doc->get("markers") : nullptr;
    if (!ms) {
        std::fprintf(stderr, "svcheck: internal error: no catalogue\n");
        return 3;
    }
    for (const lint::yaml::NodeUP &m : ms->items) {
        std::string status = str(m.get(), "status");
        if (status != "active" && status != "deprecated") {
            continue;
        }
        std::printf("%-12s %-36s %-12s %-10s %s%s\n", str(m.get(), "id").c_str(),
            str(m.get(), "name").c_str(), str(m.get(), "tier").c_str(),
            str(m.get(), "family").c_str(), str(m.get(), "source").c_str(),
            status == "deprecated" ? " (deprecated)" : "");
    }
    return 0;
}

int explainRule(lint::LintEngine &engine, const std::string &name, const std::string &format) {
    lint::yaml::NodeUP doc = parse(engine.catalogJson());
    const lint::yaml::Node *ms = doc ? doc->get("markers") : nullptr;
    if (!ms) {
        std::fprintf(stderr, "svcheck: internal error: no catalogue\n");
        return 3;
    }
    const lint::yaml::Node *hit = nullptr;
    std::vector<std::string> spellings;
    for (const lint::yaml::NodeUP &m : ms->items) {
        std::string id = str(m.get(), "id");
        std::string nm = str(m.get(), "name");
        spellings.push_back(id);
        if (!nm.empty()) {
            spellings.push_back(nm);
        }
        bool alias = false;
        if (const lint::yaml::Node *as = m->get("aliases")) {
            for (const lint::yaml::NodeUP &a : as->items) {
                alias |= a->s == name;
            }
        }
        if (!hit && (id == name || nm == name || alias)) {
            hit = m.get();
        }
    }
    if (!hit) {
        std::string near = lint::pipeline::suggest(name, spellings);
        std::fprintf(stderr, "svcheck: no rule or marker '%s'%s\n", name.c_str(),
            near.empty() ? "" : ("; did you mean '" + near + "'?").c_str());
        return 2;
    }
    std::string id = str(hit, "id");
    const lint::rules::Rule *rule = nullptr;
    for (const lint::rules::Rule *r : engine.ruleList()) {
        if (r->m_id == id) {
            rule = r;
        }
    }
    const lint::yaml::Node *rj = nullptr;
    lint::yaml::NodeUP rules;
    if (rule) {
        rules = parse(engine.rulesJson());
        if (rules) {
            for (const lint::yaml::NodeUP &r : rules->items) {
                if (str(r.get(), "id") == id) {
                    rj = r.get();
                }
            }
        }
    }
    const host::SlangWarningInfo *slang = nullptr;
    for (const host::SlangWarningInfo *s = host::slangWarnings(); s->name; s++) {
        if (host::slangMarkerId(s->num) == id) {
            slang = s;
        }
    }
    std::string summary = str(hit, "summary");
    if (summary.empty() && slang) {
        summary = slang->summary;
    }

    if (format == "json") {
        lint::JsonWriter w;
        w.beginObject().key("marker");
        writeJson(w, hit);
        if (!summary.empty()) {
            w.kv("summary", summary);
        }
        if (rj) {
            w.key("rule");
            writeJson(w, rj);
            w.kv("file", shownPath(rule->m_file)).kv("line", static_cast<int64_t>(
                rule->m_mark.line));
        }
        w.endObject();
        std::printf("%s\n", w.str().c_str());
        return 0;
    }

    std::printf("%s %s\n", id.c_str(), str(hit, "name").c_str());
    if (!summary.empty()) {
        std::printf("  %s\n", summary.c_str());
    }
    std::printf("\n  tier:      %s (%s)\n", str(hit, "tier").c_str(), str(hit, "severity").c_str());
    if (!str(hit, "family").empty()) {
        std::printf("  family:    %s\n", str(hit, "family").c_str());
    }
    if (str(hit, "status") != "active") {
        std::printf("  status:    %s\n", str(hit, "status").c_str());
    }
    if (!str(hit, "since").empty()) {
        std::printf("  since:     %s\n", str(hit, "since").c_str());
    }
    if (!list(hit, "aliases").empty()) {
        std::printf("  formerly:  %s\n", list(hit, "aliases").c_str());
    }
    if (slang) {
        std::printf("  from:      slang's warning -W%s (on by default: %s)\n", slang->name,
            slang->inDefault ? "yes" : "no");
    }
    if (rule && rj) {
        const lint::yaml::Node *m = rj->get("match");
        std::printf("  matches:   %s\n", list(m, "kind").c_str());
        std::printf("  level:     %s, scope %s\n", str(rj, "level").c_str(),
            str(rj, "scope").c_str());
        if (const lint::yaml::Node *a = rj->get("applies_to"); a && !a->members.empty()) {
            std::string s;
            for (const lint::yaml::Node::Member &am : a->members) {
                s += (s.empty() ? "" : "; ") + am.key + ": " + valueText(am.value.get());
            }
            std::printf("  applies to: %s (experimental)\n", s.c_str());
        }
        std::printf("  defined in: %s:%u\n", shownPath(rule->m_file).c_str(), rule->m_mark.line);
        if (const lint::yaml::Node *os = rj->get("options"); os && !os->items.empty()) {
            std::printf("  options:\n");
            for (const lint::yaml::NodeUP &o : os->items) {
                std::printf("    %s (%s) = %s%s%s\n", str(o.get(), "name").c_str(),
                    str(o.get(), "type").c_str(), valueText(o->get("default")).c_str(),
                    str(o.get(), "help").empty() ? "" : ": ", str(o.get(), "help").c_str());
            }
        }
        if (!rule->m_doc.empty()) {
            std::printf("\n");
            size_t start = 0;
            while (start < rule->m_doc.size()) {
                size_t nl = rule->m_doc.find('\n', start);
                std::string line = rule->m_doc.substr(start, nl == std::string::npos
                    ? std::string::npos : nl - start);
                std::printf("  %s\n", line.c_str());
                if (nl == std::string::npos) {
                    break;
                }
                start = nl + 1;
            }
        }
        if (!rule->m_examples.empty()) {
            std::printf("\n  %zu example%s (svcheck --check-rules runs them)\n",
                rule->m_examples.size(), rule->m_examples.size() == 1 ? "" : "s");
        }
    }
    return 0;
}

int checkRules(lint::LintEngine &engine, const LintOptions &opts, bool ruleErrors, bool none) {
    if (none) {
        std::fprintf(stderr, "svcheck: --check-rules: no rule files (--rules, or `rules` in "
            "--config)\n");
        return 2;
    }
    std::string uvm;
    if (!opts.uvmDir.empty()) {
        std::error_code ec;
        uvm = fs::absolute(opts.uvmDir, ec).lexically_normal().string();
    }
    lint::run::Compiler compile = host::hostCompiler(opts.layer, uvm, true);
    size_t passed = 0, failed = 0;
    lint::JsonWriter w;
    w.beginObject().key("examples").beginArray();
    for (const lint::rules::Rule *r : engine.ruleList()) {
        for (const lint::rules::Example &x : r->m_examples) {
            lint::run::ExampleResult res = lint::run::runExample(*r, x, compile,
                engine.classifiers());
            if (res.ok()) {
                passed++;
            } else {
                failed++;
                if (opts.format != "json") {
                    std::printf("%s\n", res.describe().c_str());
                }
            }
            w.beginObject().kv("rule", res.rule).kv("name", res.name)
                .kv("file", shownPath(res.file)).kv("line", static_cast<int64_t>(res.line))
                .kv("ok", res.ok());
            w.key("problems").beginArray();
            for (const std::string &p : res.problems) {
                w.value(p);
            }
            w.endArray().endObject();
        }
    }
    w.endArray().kv("rule_errors", ruleErrors).endObject();
    if (opts.format == "json") {
        std::printf("%s\n", w.str().c_str());
    } else {
        std::fprintf(stderr, "svcheck: %zu rule%s, %zu example%s passed, %zu failed%s\n",
            engine.ruleList().size(), engine.ruleList().size() == 1 ? "" : "s", passed,
            passed == 1 ? "" : "s", failed, ruleErrors ? "; the rule files have errors" : "");
    }
    return (failed || ruleErrors) ? 1 : 0;
}

int rulesFor(lint::LintEngine &engine, const std::string &selJson, const std::string &format) {
    std::string res = engine.resolveSelection(selJson);
    if (format == "json") {
        std::printf("%s\n", res.c_str());
        return 0;
    }
    lint::yaml::NodeUP doc = parse(res);
    const lint::yaml::Node *groups = doc ? doc->get("groups") : nullptr;
    const lint::yaml::Node *paths = doc ? doc->get("paths") : nullptr;
    if (!groups || !paths) {
        std::fprintf(stderr, "svcheck: internal error: the selection did not resolve\n");
        return 3;
    }
    for (const lint::yaml::NodeUP &p : paths->items) {
        int64_t g = p->get("group") ? p->get("group")->i : 0;
        const lint::yaml::Node *grp = (g >= 0 && static_cast<size_t>(g) < groups->items.size())
            ? groups->items[g].get() : nullptr;
        std::printf("%s: %s\n", shownPath(str(p.get(), "path")).c_str(), list(grp, "enabled")
            .c_str());
    }
    return 0;
}

}
}
