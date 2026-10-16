/*
 * Lint.cpp
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
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <tuple>
#include "Lint.h"
#include "LintEngine.h"
#include "SemModel.h"
#include "SvContext.h"
#include "SvLib.h"
#include "../host/Lint.h"
#include "cel/CelFunctions.h"
#include "rules/RuleLoader.h"
#include "run/Classify.h"
#include "run/Walker.h"
#include "util/JsonWriter.h"
#include "yaml/YamlDoc.h"
#include "slang/driver/Driver.h"
#include "svcheck/sem/IDecl.h"
#include "svcheck/sem/IDesign.h"
#include "svcheck/sem/IUnit.h"
#include "svcheck/sem/IUnitDep.h"
#include "svcheck/sem/UnitDepKind.h"

namespace svcheck {
namespace cli {

namespace {

namespace fs = std::filesystem;
namespace rf = svcheck::ast::reflect;
using lint::JsonWriter;
using NK = lint::yaml::Node::Kind;

// A marker of the run, as the engine's run() gives it, with its file named.
struct Finding {
    std::string     code;
    std::string     severity;
    std::string     message;
    std::string     file;           // "" for none (a tool marker about no place)
    int64_t         line = 0;
    int64_t         col = 0;
    int64_t         extent = 0;
};

// A path as shown: relative to the working directory when under it.
std::string shown(const std::string &path) {
    if (path.empty()) {
        return path;
    }
    std::error_code ec;
    fs::path cwd = fs::current_path(ec);
    fs::path p(path);
    if (!ec && p.is_absolute()) {
        fs::path rel = p.lexically_relative(cwd);
        if (!rel.empty() && *rel.begin() != "..") {
            return rel.generic_string();
        }
    }
    return p.generic_string();
}

const std::string &str(const lint::yaml::Node *n, const char *key) {
    static const std::string none;
    const lint::yaml::Node *v = n->get(key);
    return (v && v->kind == NK::String) ? v->s : none;
}

int64_t num(const lint::yaml::Node *n, const char *key) {
    const lint::yaml::Node *v = n->get(key);
    return (v && v->kind == NK::Int) ? v->i : 0;
}

bool readFile(const std::string &path, std::string &text) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    text = ss.str();
    return true;
}

const char *sarifLevel(const std::string &severity) {
    if (severity == "error") {
        return "error";
    } else if (severity == "warning") {
        return "warning";
    }
    return "note";
}

std::string sarifUri(const std::string &file) {
    std::string s = shown(file);
    return fs::path(s).is_absolute() ? "file://" + s : s;
}

void writeText(const std::vector<Finding> &found) {
    for (const Finding &f : found) {
        if (f.file.empty()) {
            std::printf("svcheck: %s: %s [%s]\n", f.severity.c_str(), f.message.c_str(),
                f.code.c_str());
        } else {
            std::printf("%s:%lld:%lld: %s: %s [%s]\n", shown(f.file).c_str(),
                static_cast<long long>(f.line), static_cast<long long>(f.col),
                f.severity.c_str(), f.message.c_str(), f.code.c_str());
        }
    }
}

void writeJson(const std::vector<Finding> &found, const host::LintCompilation &lc,
        const lint::yaml::Node *result) {
    JsonWriter w;
    w.beginObject();
    w.kv("version", SVCHECK_VERSION);
    w.key("findings").beginArray();
    for (const Finding &f : found) {
        w.beginObject().kv("code", f.code).kv("severity", f.severity).kv("message", f.message);
        w.key("file");
        if (f.file.empty()) {
            w.valueNull();
        } else {
            w.value(shown(f.file));
        }
        w.kv("line", f.line).kv("col", f.col).kv("extent", f.extent).endObject();
    }
    w.endArray();
    w.key("compilation").beginObject().kv("errors", static_cast<int64_t>(lc.errors()));
    w.key("unparsed").beginArray();
    for (const std::string &u : lc.unparsed()) {
        w.value(shown(u));
    }
    w.endArray().endObject();
    w.key("rules").beginArray();
    if (const lint::yaml::Node *stats = result->get("stats")) {
        for (const lint::yaml::NodeUP &s : stats->items) {
            w.beginObject().kv("id", str(s.get(), "id"));
            for (const char *k : {"matches", "findings", "errors", "outside", "unknown"}) {
                w.kv(k, num(s.get(), k));
            }
            w.endObject();
        }
    }
    w.endArray();
    w.endObject();
    std::printf("%s\n", w.str().c_str());
}

void writeSarif(const std::vector<Finding> &found, const lint::LintEngine &engine) {
    JsonWriter w;
    w.beginObject();
    w.kv("$schema", "https://json.schemastore.org/sarif-2.1.0.json").kv("version", "2.1.0");
    w.key("runs").beginArray().beginObject();
    w.key("tool").beginObject().key("driver").beginObject();
    w.kv("name", "svcheck").kv("version", SVCHECK_VERSION);
    w.kv("informationUri", "https://github.com/fvutils/svcheck");
    w.key("rules").beginArray();
    for (const lint::rules::Rule *r : engine.ruleList()) {
        w.beginObject().kv("id", r->m_id).kv("name", r->m_name);
        w.key("shortDescription").beginObject().kv("text", r->m_summary).endObject();
        w.endObject();
    }
    w.endArray();
    w.endObject().endObject();
    w.key("results").beginArray();
    for (const Finding &f : found) {
        w.beginObject().kv("ruleId", f.code).kv("level", sarifLevel(f.severity));
        w.key("message").beginObject().kv("text", f.message).endObject();
        if (!f.file.empty()) {
            w.key("locations").beginArray().beginObject().key("physicalLocation").beginObject();
            w.key("artifactLocation").beginObject().kv("uri", sarifUri(f.file)).endObject();
            w.key("region").beginObject().kv("startLine", f.line).kv("startColumn", f.col);
            if (f.extent > 0) {
                w.kv("endColumn", f.col + f.extent);
            }
            w.endObject();
            w.endObject().endObject().endArray();
        }
        w.endObject();
    }
    w.endArray();
    w.endObject().endArray();
    w.endObject();
    std::printf("%s\n", w.str().c_str());
}

const char *depKindName(sem::UnitDepKind k) {
    switch (k) {
    case sem::UnitDepKind::Import: return "Import";
    case sem::UnitDepKind::TypeRef: return "TypeRef";
    case sem::UnitDepKind::Include: return "Include";
    case sem::UnitDepKind::HierRef: return "HierRef";
    case sem::UnitDepKind::VirtualInterface: return "VirtualInterface";
    }
    return "?";
}

// `--contexts` (sv-contexts.md §3.2): each unit, its labels, its carriers and
// the evidence for each label; the dependencies between units; the notes.
int printContexts(lint::LintEngine &engine, host::LintCompilation &lc,
        const std::string &format) {
    host::SvContext *sv = lc.svContext();
    if (!sv) {
        std::fprintf(stderr, "svcheck: no contexts: %s\n", lc.errors()
            ? ("the compilation has " + std::to_string(lc.errors()) + " errors").c_str()
            : "contexts are classified on the curated model (--semantic=model)");
        return 1;
    }
    lint::run::Contexts found = engine.classify(lc.roots(), lc.context());
    sem::IDesign *design = sv->design();
    auto pathOf = [&lc](int32_t fileid) {
        return (fileid >= 0 && static_cast<size_t>(fileid) < lc.paths().size())
            ? shown(lc.paths()[fileid]) : std::string("?");
    };
    auto kindOf = [](sem::IDecl *d) {
        return std::string(rf::classes[host::semNode(static_cast<sem::ISemNode *>(d)).kind].name);
    };
    auto noteWhere = [&](const lint::run::ContextNote &n) {
        ast::Location loc;
        if (n.carrier && lint::run::Walker::location(n.carrier, loc)) {
            return pathOf(loc.fileid) + ":" + std::to_string(loc.lineno);
        }
        return std::string();
    };

    if (format == "json") {
        JsonWriter w;
        w.beginObject().key("units").beginArray();
        for (const sem::IUnitUP &u : design->getUnits()) {
            w.beginObject().kv("name", u->getName());
            w.key("labels").beginArray();
            for (const std::string &l : u->getLabels()) {
                w.value(l);
            }
            w.endArray().key("carriers").beginArray();
            for (sem::IDecl *d : u->getCarriers()) {
                w.beginObject().kv("kind", kindOf(d)).kv("name", d->getQname());
                w.kv("file", pathOf(d->getLocation().fileid));
                w.kv("line", static_cast<int64_t>(d->getLocation().lineno)).endObject();
            }
            w.endArray().key("evidence").beginArray();
            for (const std::string &e : u->getEvidence()) {
                w.value(e);
            }
            w.endArray().endObject();
        }
        w.endArray().key("deps").beginArray();
        for (const sem::IUnitDepUP &d : design->getDeps()) {
            w.beginObject().kv("from", d->getFrom()->getName()).kv("to", d->getTo()->getName());
            w.kv("kind", depKindName(d->getKind())).endObject();
        }
        w.endArray().key("notes").beginArray();
        for (const lint::run::ContextNote &n : found.notes) {
            w.beginObject().kv("where", noteWhere(n)).kv("message", n.message).endObject();
        }
        w.endArray().endObject();
        std::printf("%s\n", w.str().c_str());
        return 0;
    }

    if (design->getUnits().empty()) {
        std::printf("no units: no classifier labelled a carrier\n");
    }
    for (const sem::IUnitUP &u : design->getUnits()) {
        std::string labels;
        for (const std::string &l : u->getLabels()) {
            labels += (labels.empty() ? "" : ", ") + l;
        }
        std::printf("unit %s: %s\n", u->getName().c_str(), labels.c_str());
        for (sem::IDecl *d : u->getCarriers()) {
            std::printf("  %s %s  %s:%d\n", kindOf(d).c_str(), d->getQname().c_str(),
                pathOf(d->getLocation().fileid).c_str(), d->getLocation().lineno);
        }
        for (const std::string &e : u->getEvidence()) {
            std::printf("    %s\n", e.c_str());
        }
    }
    if (!design->getDeps().empty()) {
        std::printf("dependencies:\n");
        for (const sem::IUnitDepUP &d : design->getDeps()) {
            std::printf("  %s -> %s (%s)\n", d->getFrom()->getName().c_str(),
                d->getTo()->getName().c_str(), depKindName(d->getKind()));
        }
    }
    if (!found.notes.empty()) {
        std::printf("notes:\n");
        for (const lint::run::ContextNote &n : found.notes) {
            std::string where = noteWhere(n);
            std::printf("  %s%s%s\n", where.c_str(), where.empty() ? "" : ": ",
                n.message.c_str());
        }
    }
    return 0;
}

}

bool isFormat(const std::string &name) {
    return name == "text" || name == "json" || name == "sarif";
}

int runLint(slang::driver::Driver &driver, const LintOptions &opts) {
    host::installSchema(opts.layer);
    lint::cel::setHostLib(&host::registerSvLib);

    lint::LintEngine engine;
    engine.setReader(lint::rules::diskReader());
    bool ruleErrors = false;
    for (const std::string &path : opts.rules) {
        lint::LintSource src;
        src.name = path;
        if (!readFile(path, src.text)) {
            std::fprintf(stderr, "svcheck: cannot read rule file '%s'\n", path.c_str());
            return 2;
        }
        ruleErrors |= engine.loadRules(src, false) == nullptr;
    }
    static const char *sevNames[] = {"error", "warning", "info"};
    for (const lint::RuleDiagnostic &d : engine.diagnostics()) {
        std::fprintf(stderr, "%s:%u:%u: %s: %s [%s]\n", d.file.c_str(), d.line, d.col,
            sevNames[static_cast<int>(d.severity)], d.message.c_str(), d.code.c_str());
        ruleErrors |= d.severity == lint::RuleDiagnostic::Severity::Error;
    }
    if (ruleErrors) {
        std::fprintf(stderr, "svcheck: the rule files have errors; nothing was checked\n");
        return 2;
    }

    if (!driver.processOptions()) {
        return 2;
    }
    (void)driver.parseAllSources();
    host::LintCompilation lc(driver, opts.layer, opts.libraryDirs);
    (void)driver.reportDiagnostics(/*quiet*/ true);
    for (const std::string &u : lc.unparsed()) {
        std::fprintf(stderr, "svcheck: %s has syntax errors; no rule ran on it\n",
            shown(u).c_str());
    }

    if (opts.contexts) {
        return printContexts(engine, lc, opts.format);
    }

    // Design §3.3: a rule that needs the compilation does not run on a
    // broken one; it is reported once, and the rest run.
    JsonWriter req;
    req.beginObject().key("not_run").beginObject();
    if (lc.errors()) {
        std::string why = "it needs the compilation, which has " + std::to_string(lc.errors())
            + (lc.errors() == 1 ? " error" : " errors");
        for (const lint::rules::Rule *r : engine.ruleList()) {
            if (host::needsCompilation(*r)) {
                req.kv(r->m_id, why);
            }
        }
    }
    req.endObject().endObject();

    std::string out = engine.run(lc.roots(), req.str(), &lc.context());
    lint::yaml::NodeUP result;
    try {
        result = lint::yaml::parse("<lint result>", out);
    } catch (const lint::yaml::YamlError &e) {
        std::fprintf(stderr, "svcheck: internal error: %s\n", e.what());
        return 2;
    }
    if (!result || !result->get("ok") || !result->get("ok")->b) {
        std::fprintf(stderr, "svcheck: internal error: %s\n", out.c_str());
        return 2;
    }
    if (const lint::yaml::Node *ps = result->get("problems")) {
        for (const lint::yaml::NodeUP &p : ps->items) {
            std::fprintf(stderr, "svcheck: %s\n", p->s.c_str());
        }
    }

    std::vector<Finding> found;
    for (const lint::yaml::NodeUP &m : result->get("markers")->items) {
        Finding f;
        f.code = str(m.get(), "code");
        f.severity = str(m.get(), "severity");
        f.message = str(m.get(), "message");
        int64_t fileid = num(m.get(), "fileid");
        f.line = num(m.get(), "line");
        if (fileid >= 0 && f.line > 0 && static_cast<size_t>(fileid) < lc.paths().size()) {
            f.file = lc.paths()[fileid];
            f.col = num(m.get(), "col");
            f.extent = std::max<int64_t>(num(m.get(), "extent"), 0);
        } else {
            f.line = 0;
        }
        found.push_back(std::move(f));
    }
    // Tool markers about no place first, then by place.
    std::stable_sort(found.begin(), found.end(), [](const Finding &a, const Finding &b) {
        return std::make_tuple(!a.file.empty(), a.file, a.line, a.col)
            < std::make_tuple(!b.file.empty(), b.file, b.line, b.col);
    });

    if (opts.format == "json") {
        writeJson(found, lc, result.get());
    } else if (opts.format == "sarif") {
        writeSarif(found, engine);
    } else {
        writeText(found);
    }

    size_t errors = 0, warnings = 0;
    for (const Finding &f : found) {
        errors += f.severity == "error";
        warnings += f.severity == "warning";
    }
    if (opts.format == "text") {
        std::fflush(stdout);
        std::string compile = lc.errors() ? "; the compilation has "
            + std::to_string(lc.errors()) + (lc.errors() == 1 ? " error" : " errors") : "";
        std::fprintf(stderr, "svcheck: %zu error%s, %zu warning%s%s\n", errors,
            errors == 1 ? "" : "s", warnings, warnings == 1 ? "" : "s", compile.c_str());
    }
    return (errors || warnings || lc.errors()) ? 1 : 0;
}

}
}
