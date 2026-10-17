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
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include "Lint.h"
#include "LintEngine.h"
#include "SemModel.h"
#include "SvContext.h"
#include "SvLib.h"
#include "SlangWarnings.h"
#include "Config.h"
#include "Query.h"
#include "../host/Lint.h"
#include "cel/CelFunctions.h"
#include "rules/RuleLoader.h"
#include "run/Classify.h"
#include "run/Walker.h"
#include "util/JsonWriter.h"
#include "yaml/YamlDoc.h"
#include "slang/driver/Driver.h"
#include "slang/syntax/SyntaxTree.h"
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

// One label of a finding's context (sv-contexts.md §5.4).
struct FindingLabel {
    std::string     axis;
    std::string     label;
    std::string     unit;
};

// A marker of the run, as the engine's run() gives it, with its file named;
// or one of slang's warnings, or a waiver hygiene marker.
struct Finding {
    std::string     code;
    std::string     severity;
    std::string     message;
    std::string     file;           // "" for none (a tool marker about no place)
    int64_t         line = 0;
    int64_t         col = 0;
    int64_t         extent = 0;
    std::vector<FindingLabel>   context;
    std::string     waivedBy;       // a waived finding's reason ("" when not waived)
    bool            waived = false;
    std::string     originalSeverity;   // before -Werror promoted it
};

// `--stats`: each phase's time, and the heap in use after it.
struct RunStats {
    double  parseS = 0;
    double  totalS = 0;
    size_t  heapParsed = 0;
    size_t  heapEnd = 0;
};

double nsToS(int64_t ns) {
    return static_cast<double>(ns) / 1e9;
}

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
        std::string waived = f.waived ? " [waived: " + f.waivedBy + "]" : "";
        if (f.file.empty()) {
            std::printf("svcheck: %s: %s [%s]%s\n", f.severity.c_str(), f.message.c_str(),
                f.code.c_str(), waived.c_str());
        } else {
            std::printf("%s:%lld:%lld: %s: %s [%s]%s\n", shown(f.file).c_str(),
                static_cast<long long>(f.line), static_cast<long long>(f.col),
                f.severity.c_str(), f.message.c_str(), f.code.c_str(), waived.c_str());
        }
    }
}

// GitHub Actions workflow commands: one annotation per finding.
void writeGithub(const std::vector<Finding> &found) {
    auto esc = [](const std::string &s, bool prop) {
        std::string o;
        for (char c : s) {
            if (c == '%') o += "%25";
            else if (c == '\r') o += "%0D";
            else if (c == '\n') o += "%0A";
            else if (prop && c == ':') o += "%3A";
            else if (prop && c == ',') o += "%2C";
            else o += c;
        }
        return o;
    };
    for (const Finding &f : found) {
        const char *level = f.severity == "error" ? "error"
            : f.severity == "warning" ? "warning" : "notice";
        std::string props = "title=" + esc(f.code, true);
        if (!f.file.empty()) {
            props = "file=" + esc(shown(f.file), true) + ",line=" + std::to_string(f.line)
                + ",col=" + std::to_string(f.col) + "," + props;
        }
        std::printf("::%s %s::%s\n", level, props.c_str(), esc(f.message, false).c_str());
    }
}

void writeContextJson(JsonWriter &w, const std::vector<FindingLabel> &ctx) {
    w.key("context").beginArray();
    for (const FindingLabel &l : ctx) {
        w.beginObject().kv("axis", l.axis).kv("label", l.label).kv("unit", l.unit).endObject();
    }
    w.endArray();
}

int64_t num(const lint::yaml::Node *n, const char *key);

void writeStatsJson(JsonWriter &w, const RunStats &rs, const host::LintCompilation &lc,
        const lint::yaml::Node *result) {
    const host::LintCompilation::Stats &cs = lc.stats();
    w.key("stats").beginObject();
    w.kv("parse_s", rs.parseS).kv("elaborate_s", cs.elaborateS).kv("lower_s", cs.lowerS);
    w.kv("classify_s", nsToS(num(result, "classify_ns")));
    w.kv("rules_s", nsToS(num(result, "run_ns")));
    w.kv("index_s", nsToS(num(result, "index_ns")));
    w.kv("total_s", rs.totalS);
    w.key("heap").beginObject();
    w.kv("parsed", static_cast<uint64_t>(rs.heapParsed));
    w.kv("elaborated", static_cast<uint64_t>(cs.heapElaborated));
    w.kv("lowered", static_cast<uint64_t>(cs.heapLowered));
    w.kv("end", static_cast<uint64_t>(rs.heapEnd)).endObject();
    w.kv("max_rss", static_cast<uint64_t>(host::peakRss()));
    w.key("rules").beginArray();
    if (const lint::yaml::Node *stats = result->get("stats")) {
        for (const lint::yaml::NodeUP &st : stats->items) {
            w.beginObject().kv("id", str(st.get(), "id"));
            w.kv("s", nsToS(num(st.get(), "ns"))).kv("steps", num(st.get(), "steps"));
            w.endObject();
        }
    }
    w.endArray().endObject();
}

void writeStatsText(const RunStats &rs, const host::LintCompilation &lc,
        const lint::yaml::Node *result) {
    const host::LintCompilation::Stats &cs = lc.stats();
    auto mb = [](size_t b) { return static_cast<double>(b) / (1024.0 * 1024.0); };
    std::fprintf(stderr, "svcheck: stats\n");
    std::fprintf(stderr, "  parse      %8.3f s   heap %8.1f MB\n", rs.parseS, mb(rs.heapParsed));
    std::fprintf(stderr, "  elaborate  %8.3f s   heap %8.1f MB\n", cs.elaborateS,
        mb(cs.heapElaborated));
    std::fprintf(stderr, "  lower      %8.3f s   heap %8.1f MB\n", cs.lowerS,
        mb(cs.heapLowered));
    std::fprintf(stderr, "  index      %8.3f s\n", nsToS(num(result, "index_ns")));
    std::fprintf(stderr, "  classify   %8.3f s\n", nsToS(num(result, "classify_ns")));
    std::fprintf(stderr, "  rules      %8.3f s\n", nsToS(num(result, "run_ns")));
    std::fprintf(stderr, "  total      %8.3f s   heap %8.1f MB, peak RSS %.1f MB\n",
        rs.totalS, mb(rs.heapEnd), mb(host::peakRss()));
    if (const lint::yaml::Node *stats = result->get("stats")) {
        for (const lint::yaml::NodeUP &st : stats->items) {
            std::fprintf(stderr, "    %-12s %8.3f s  %lld matches\n", str(st.get(), "id").c_str(),
                nsToS(num(st.get(), "ns")), static_cast<long long>(num(st.get(), "matches")));
        }
    }
}

void writeFindingJson(JsonWriter &w, const Finding &f) {
    w.beginObject().kv("code", f.code).kv("severity", f.severity).kv("message", f.message);
    w.key("file");
    if (f.file.empty()) {
        w.valueNull();
    } else {
        w.value(shown(f.file));
    }
    w.kv("line", f.line).kv("col", f.col).kv("extent", f.extent);
    if (!f.context.empty()) {
        writeContextJson(w, f.context);
    }
    if (!f.originalSeverity.empty()) {
        w.kv("original_severity", f.originalSeverity);
    }
    if (f.waived) {
        w.kv("waiver", f.waivedBy);
    }
    w.endObject();
}

void writeFindingsJson(const std::vector<Finding> &found, const std::vector<Finding> &waived,
        const host::LintCompilation &lc, const lint::yaml::Node *result, const RunStats *rs) {
    JsonWriter w;
    w.beginObject();
    w.kv("version", SVCHECK_VERSION);
    w.key("findings").beginArray();
    for (const Finding &f : found) {
        writeFindingJson(w, f);
    }
    w.endArray();
    w.key("waived").beginArray();
    for (const Finding &f : waived) {
        writeFindingJson(w, f);
    }
    w.endArray();
    w.key("compilation").beginObject().kv("errors", static_cast<int64_t>(lc.errors()));
    w.key("unparsed").beginArray();
    for (const std::string &u : lc.unparsed()) {
        w.value(shown(u));
    }
    w.endArray().key("files").beginArray();
    for (const std::string &f : lc.files()) {
        w.value(shown(f));
    }
    w.endArray().endObject();
    w.key("rules").beginArray();
    if (const lint::yaml::Node *stats = result->get("stats")) {
        for (const lint::yaml::NodeUP &s : stats->items) {
            w.beginObject().kv("id", str(s.get(), "id")).kv("tier", str(s.get(), "tier"));
            for (const char *k : {"matches", "findings", "errors", "outside", "unknown"}) {
                w.kv(k, num(s.get(), k));
            }
            w.endObject();
        }
    }
    w.endArray();
    if (rs) {
        writeStatsJson(w, *rs, lc, result);
    }
    w.endObject();
    std::printf("%s\n", w.str().c_str());
}

void writeSarif(const std::vector<Finding> &found, const std::vector<Finding> &waived,
        const lint::LintEngine &engine, const lint::yaml::Node *result) {
    // The rules that ran (the result's stats), not every rule loaded: the
    // built-in packs are loaded whether or not the selection enables them.
    std::set<std::string> ran;
    if (const lint::yaml::Node *stats = result ? result->get("stats") : nullptr) {
        for (const lint::yaml::NodeUP &st : stats->items) {
            ran.insert(str(st.get(), "id"));
        }
    }
    JsonWriter w;
    w.beginObject();
    w.kv("$schema", "https://json.schemastore.org/sarif-2.1.0.json").kv("version", "2.1.0");
    w.key("runs").beginArray().beginObject();
    w.key("tool").beginObject().key("driver").beginObject();
    w.kv("name", "svcheck").kv("version", SVCHECK_VERSION);
    w.kv("informationUri", "https://github.com/fvutils/svcheck");
    w.key("rules").beginArray();
    std::set<std::string> described;
    for (const lint::rules::Rule *r : engine.ruleList()) {
        if (!ran.count(r->m_id)) {
            continue;
        }
        w.beginObject().kv("id", r->m_id).kv("name", r->m_name);
        w.key("shortDescription").beginObject().kv("text", r->m_summary).endObject();
        w.endObject();
        described.insert(r->m_id);
    }
    // slang's warnings that were reported (Q-UVM4).
    for (const std::vector<Finding> *list : {&found, &waived}) {
        for (const Finding &f : *list) {
            if (f.code.compare(0, 5, "SLANG") != 0 || !described.insert(f.code).second) {
                continue;
            }
            for (const host::SlangWarningInfo *s = host::slangWarnings(); s->name; s++) {
                if (host::slangMarkerId(s->num) == f.code) {
                    w.beginObject().kv("id", f.code).kv("name", std::string(s->name));
                    w.key("shortDescription").beginObject().kv("text", std::string(s->summary))
                        .endObject();
                    w.endObject();
                }
            }
        }
    }
    w.endArray();
    w.endObject().endObject();
    w.key("results").beginArray();
    std::vector<const Finding *> all;
    for (const Finding &f : found) {
        all.push_back(&f);
    }
    for (const Finding &f : waived) {
        all.push_back(&f);
    }
    for (const Finding *fp : all) {
        const Finding &f = *fp;
        w.beginObject().kv("ruleId", f.code).kv("level", sarifLevel(f.severity));
        w.key("message").beginObject().kv("text", f.message).endObject();
        // A waived finding is a suppressed result (SARIF §3.27.23).
        if (f.waived) {
            w.key("suppressions").beginArray().beginObject().kv("kind", "inSource")
                .kv("justification", f.waivedBy).endObject().endArray();
        }
        if (!f.context.empty()) {
            w.key("properties").beginObject();
            writeContextJson(w, f.context);
            w.endObject();
        }
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
            w.endArray().key("convention_labels").beginArray();
            for (const std::string &l : u->getConventionLabels()) {
                w.value(l);
            }
            w.endArray().key("evidence").beginArray();
            for (const std::string &e : u->getEvidence()) {
                w.value(e);
            }
            w.endArray().key("disagreements").beginArray();
            for (const std::string &e : u->getDisagreements()) {
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
        // A label only convention gives never selects (Q-CX4): shown apart.
        std::string conv;
        for (const std::string &l : u->getConventionLabels()) {
            conv += (conv.empty() ? "" : ", ") + l;
        }
        if (!conv.empty()) {
            labels += (labels.empty() ? "" : "; ") + std::string("convention only: ") + conv;
        }
        std::printf("unit %s: %s\n", u->getName().c_str(), labels.c_str());
        for (sem::IDecl *d : u->getCarriers()) {
            std::printf("  %s %s  %s:%d\n", kindOf(d).c_str(), d->getQname().c_str(),
                pathOf(d->getLocation().fileid).c_str(), d->getLocation().lineno);
        }
        for (const std::string &e : u->getEvidence()) {
            std::printf("    %s\n", e.c_str());
        }
        for (const std::string &e : u->getDisagreements()) {
            std::printf("    disagreement: %s\n", e.c_str());
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
    return name == "text" || name == "json" || name == "sarif" || name == "github";
}

void takeWarningPolicy(std::vector<std::string> &args, LintOptions &opts,
                       const std::function<bool(const std::string &)> &slangName) {
    std::vector<std::string> rest;
    auto take = [&](const std::string &spec) {
        if (spec == "error") {
            opts.werrorAll = true;
        } else if (spec.rfind("error=", 0) == 0) {
            opts.werror.push_back(spec.substr(6));
        } else if (spec.rfind("no-error=", 0) == 0) {
            opts.wnoError.push_back(spec.substr(9));
            if (slangName && slangName(spec.substr(9)))
                rest.push_back("-W" + spec);
        } else {
            return false;
        }
        return true;
    };
    for (size_t i=0; i<args.size(); i++) {
        const std::string &a = args[i];
        if (a == "-W" && i + 1 < args.size() && take(args[i + 1])) {
            i++;
        } else if (a.size() > 2 && a.compare(0, 2, "-W") == 0 && take(a.substr(2))) {
            continue;
        } else {
            rest.push_back(a);
        }
    }
    args.swap(rest);
}

namespace {

std::string absPath(const std::string &p) {
    std::error_code ec;
    fs::path a = fs::absolute(fs::path(p), ec);
    return ec ? p : a.lexically_normal().string();
}

lint::yaml::NodeUP parseJson(const std::string &what, const std::string &text) {
    try {
        return lint::yaml::parse(what, text);
    } catch (const lint::yaml::YamlError &) {
        return nullptr;
    }
}

// Loads each rule file, or each rule file in a directory (its *.yaml and
// *.yml that are rule files, in name order). False when one cannot be read.
bool loadRuleSources(lint::LintEngine &engine, const std::vector<std::string> &paths,
        bool &ruleErrors) {
    for (const std::string &path : paths) {
        std::vector<std::string> files;
        std::error_code ec;
        bool dir = fs::is_directory(path, ec);
        if (dir) {
            for (const fs::directory_entry &e : fs::directory_iterator(path, ec)) {
                std::string ext = e.path().extension().string();
                if (e.is_regular_file() && (ext == ".yaml" || ext == ".yml")) {
                    files.push_back(e.path().string());
                }
            }
            std::sort(files.begin(), files.end());
        } else {
            files.push_back(path);
        }
        for (const std::string &f : files) {
            lint::LintSource src;
            src.name = f;
            if (!readFile(f, src.text)) {
                std::fprintf(stderr, "svcheck: cannot read rule file '%s'\n", f.c_str());
                return false;
            }
            // In a directory, a file without `sv-rules` is not a rule file,
            // and one without `rules:` is a helper file that others `uses:`.
            if (dir && engine.probeRules(src.text) != lint::RuleProbe::RuleFile) {
                continue;
            }
            if (dir) {
                lint::yaml::NodeUP doc = parseJson(f, src.text);
                const lint::yaml::Node *root = doc ? doc->get("sv-rules") : nullptr;
                if (root && root->kind == NK::Map && !root->get("rules")) {
                    continue;
                }
            }
            ruleErrors |= engine.loadRules(src, false) == nullptr;
        }
    }
    return true;
}

// The selection input (pipeline/Selection.h) from the configuration and the
// command line; with `paths`, the files to place.
std::string selectionJson(const LintOptions &opts, const Config &cfg,
        const std::vector<std::string> *paths) {
    const lint::yaml::Node *root = cfg.root;
    auto get = [root](const char *k) { return root ? root->get(k) : nullptr; };
    JsonWriter w;
    w.beginObject().key("lint").beginObject();
    for (const char *k : {"select", "ignore", "rules-version", "rule", "overrides"}) {
        if (const lint::yaml::Node *n = get(k)) {
            w.key(k);
            writeJson(w, n);
        }
    }
    if (!opts.profile.empty()) {
        w.kv("profile", opts.profile);
    } else if (const lint::yaml::Node *n = get("profile")) {
        w.key("profile");
        writeJson(w, n);
    }
    w.endObject();
    w.key("command_line").beginObject();
    w.key("select").beginArray();
    for (const std::string &x : opts.select) {
        w.value(x);
    }
    w.endArray().key("ignore").beginArray();
    for (const std::string &x : opts.ignore) {
        w.value(x);
    }
    w.endArray().endObject();
    if (const lint::yaml::Node *n = get("severity")) {
        w.key("severity");
        writeJson(w, n);
    }
    // The -W policy: the configuration's `warnings`, then the command line's.
    const lint::yaml::Node *cw = get("warnings");
    auto cwGet = [cw](const char *k) {
        return (cw && cw->kind == lint::yaml::Node::Kind::Map) ? cw->get(k) : nullptr;
    };
    w.key("warnings").beginObject();
    const lint::yaml::Node *ce = cwGet("error");
    bool all = opts.werrorAll || (ce && ce->kind == NK::Bool && ce->b);
    if (all) {
        w.kv("error", true);
    } else {
        w.key("error").beginArray();
        if (ce && ce->kind == NK::Seq) {
            for (const lint::yaml::NodeUP &i : ce->items) {
                writeJson(w, i.get());
            }
        }
        for (const std::string &x : opts.werror) {
            w.value(x);
        }
        w.endArray();
    }
    w.key("no-error").beginArray();
    if (const lint::yaml::Node *ne = cwGet("no-error"); ne && ne->kind == NK::Seq) {
        for (const lint::yaml::NodeUP &i : ne->items) {
            writeJson(w, i.get());
        }
    }
    for (const std::string &x : opts.wnoError) {
        w.value(x);
    }
    w.endArray();
    const lint::yaml::Node *cn = cwGet("none");
    w.kv("none", opts.noWarnings || (cn && cn->kind == NK::Bool && cn->b));
    w.endObject();
    std::error_code ec;
    w.kv("base_dir", cfg.dir.empty() ? fs::current_path(ec).string() : cfg.dir);
    if (paths) {
        w.key("paths").beginArray();
        for (const std::string &p : *paths) {
            w.value(p);
        }
        w.endArray();
    }
    w.endObject();
    return w.str();
}

// Every user file the command line names and what they include, before
// the compilation: where -W groups are resolved for slang's warnings.
std::vector<std::string> sourcePaths(slang::driver::Driver &driver) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const auto &t : driver.syntaxTrees) {
        for (slang::BufferID buf : t->getSourceBufferIds()) {
            std::string p = driver.sourceManager.getFullPath(buf).string();
            if (!p.empty() && seen.insert(p).second) {
                out.push_back(p);
            }
        }
    }
    return out;
}

void printStatistics(const std::vector<Finding> &found, FILE *out) {
    std::map<std::string, size_t> n;
    for (const Finding &f : found) {
        n[f.code]++;
    }
    for (const auto &c : n) {
        std::fprintf(out, "%6zu  %s\n", c.second, c.first.c_str());
    }
}

}

int runLint(slang::driver::Driver &driver, const LintOptions &opts) {
    host::installSchema(opts.layer);
    lint::cel::setHostLib(&host::registerSvLib);

    Config cfg;
    if (!opts.config.empty()) {
        std::string err;
        if (!readConfig(opts.config, cfg, err)) {
            std::fprintf(stderr, "%s", err.c_str());
            std::fprintf(stderr, "svcheck: the configuration has errors; nothing was checked\n");
            return 2;
        }
    }

    lint::LintEngine engine;
    engine.setReader(lint::rules::diskReader());
    // The built-in packs (rules/*.yaml) follow the profile's tiers, so
    // loading them changes nothing until a profile or selector asks. They
    // read the model, so they are not loaded without it (--semantic=none).
    if (opts.layer == host::SemanticLayer::Model) {
        engine.loadBuiltinRules();
    }
    bool ruleErrors = false;
    std::vector<std::string> rulePaths = cfg.rules;
    rulePaths.insert(rulePaths.end(), opts.rules.begin(), opts.rules.end());
    if (!loadRuleSources(engine, rulePaths, ruleErrors)) {
        return 2;
    }
    static const char *sevNames[] = {"error", "warning", "info"};
    for (const lint::RuleDiagnostic &d : engine.diagnostics()) {
        std::fprintf(stderr, "%s:%u:%u: %s: %s [%s]\n", d.file.c_str(), d.line, d.col,
            sevNames[static_cast<int>(d.severity)], d.message.c_str(), d.code.c_str());
        ruleErrors |= d.severity == lint::RuleDiagnostic::Severity::Error;
    }
    if (opts.checkRules) {
        return checkRules(engine, opts, ruleErrors, rulePaths.empty());
    }
    if (ruleErrors) {
        std::fprintf(stderr, "svcheck: the rule files have errors; nothing was checked\n");
        return 2;
    }

    // slang's warnings are markers too (Q-UVM4); then the selection, which
    // is checked before anything is compiled.
    engine.setHostMarkers(host::slangHostMarkersJson());
    std::string selJson = selectionJson(opts, cfg, nullptr);
    lint::yaml::NodeUP selRes = parseJson("<selection>", engine.resolveSelection(selJson));
    if (!selRes || !selRes->get("ok") || !selRes->get("ok")->b) {
        std::fprintf(stderr, "svcheck: internal error: the selection did not resolve\n");
        return 3;
    }
    if (const lint::yaml::Node *errs = selRes->get("errors"); errs && !errs->items.empty()) {
        for (const lint::yaml::NodeUP &e : errs->items) {
            std::fprintf(stderr, "svcheck: %s: %s\n", str(e.get(), "key").c_str(),
                str(e.get(), "message").c_str());
        }
        std::fprintf(stderr, "svcheck: the selection has errors; nothing was checked\n");
        return 2;
    }
    if (opts.listMarkers) {
        return listMarkers(engine, opts.format);
    }
    if (!opts.explain.empty()) {
        return explainRule(engine, opts.explain, opts.format);
    }
    if (opts.showConfig) {
        std::printf("%s\n", engine.resolveSelection(selJson).c_str());
        return 0;
    }

    if (!driver.processOptions()) {
        return 2;
    }
    if (opts.rulesFor) {
        std::vector<std::string> paths;
        for (const fs::path &p : driver.sourceLoader.getFilePaths()) {
            paths.push_back(absPath(p.string()));
        }
        return rulesFor(engine, selectionJson(opts, cfg, &paths), opts.format);
    }

    using Clock = std::chrono::steady_clock;
    RunStats rs;
    auto t0 = Clock::now();
    (void)driver.parseAllSources();
    rs.parseS = std::chrono::duration<double>(Clock::now() - t0).count();
    rs.heapParsed = host::heapInUse();

    // slang reports what svcheck's selection enables (Q-UVM4): those not in
    // its default group are turned on, before the user's own -W options, so
    // that a -Wno-x still silences x.
    {
        std::vector<std::string> paths = sourcePaths(driver);
        lint::yaml::NodeUP res = parseJson("<selection>",
            engine.resolveSelection(selectionJson(opts, cfg, &paths)));
        std::set<std::string> enabled;
        if (const lint::yaml::Node *gs = res ? res->get("groups") : nullptr) {
            for (const lint::yaml::NodeUP &g : gs->items) {
                if (const lint::yaml::Node *en = g->get("enabled")) {
                    for (const lint::yaml::NodeUP &id : en->items) {
                        enabled.insert(id->s);
                    }
                }
            }
        }
        std::vector<std::string> wopts = host::slangWarningOptions(enabled);
        if (!wopts.empty()) {
            const std::vector<std::string> &mine = driver.options.warningOptions;
            wopts.insert(wopts.end(), mine.begin(), mine.end());
            driver.diagEngine.setWarningOptions(wopts);
        }
    }

    host::LintCompilation lc(driver, opts.layer, opts.libraryDirs);
    (void)driver.reportDiagnostics(/*quiet*/ true);
    for (const std::string &u : lc.unparsed()) {
        std::fprintf(stderr, "svcheck: %s has syntax errors; its findings are not shown\n",
            shown(u).c_str());
    }
    if (host::SvContext *sv = lc.svContext(); sv && !cfg.contexts.empty()) {
        std::vector<host::ConfigContext> ccs;
        for (size_t i=0; i<cfg.contexts.size(); i++) {
            const DeclaredContextRule &d = cfg.contexts[i];
            ccs.push_back({d.files, d.packages, d.label, d.unit, d.axis,
                shown(cfg.path) + ": svcheck.contexts[" + std::to_string(i) + "]"});
        }
        sv->setConfigContexts(ccs, cfg.dir);
    }

    if (opts.contexts) {
        return printContexts(engine, lc, opts.format);
    }

    // Design §3.3: a rule that needs the compilation does not run on a
    // broken one; it is reported once, and the rest run.
    JsonWriter req;
    req.beginObject().key("selection").raw(selJson).key("not_run").beginObject();
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
    lint::yaml::NodeUP result = parseJson("<lint result>", out);
    if (!result || !result->get("ok") || !result->get("ok")->b) {
        std::fprintf(stderr, "svcheck: internal error: %s\n", out.c_str());
        return 3;
    }
    if (const lint::yaml::Node *ps = result->get("problems")) {
        for (const lint::yaml::NodeUP &p : ps->items) {
            std::fprintf(stderr, "svcheck: %s\n", p->s.c_str());
        }
    }

    std::vector<Finding> found;
    std::vector<std::string> ran;
    if (const lint::yaml::Node *stats = result->get("stats")) {
        for (const lint::yaml::NodeUP &st : stats->items) {
            ran.push_back(str(st.get(), "id"));
        }
    }
    for (const lint::yaml::NodeUP &m : result->get("markers")->items) {
        Finding f;
        f.code = str(m.get(), "code");
        f.severity = str(m.get(), "severity");
        f.message = str(m.get(), "message");
        int64_t fileid = num(m.get(), "fileid");
        f.line = num(m.get(), "line");
        if (fileid >= 0 && f.line > 0 && static_cast<size_t>(fileid) < lc.paths().size()) {
            f.file = absPath(lc.paths()[fileid]);
            f.col = num(m.get(), "col");
            f.extent = std::max<int64_t>(num(m.get(), "extent"), 0);
        } else {
            f.line = 0;
        }
        if (!f.file.empty() && !lc.shows(f.file)) {
            continue;
        }
        if (const lint::yaml::Node *ctx = m->get("context")) {
            for (const lint::yaml::NodeUP &c : ctx->items) {
                f.context.push_back({str(c.get(), "axis"), str(c.get(), "label"),
                    str(c.get(), "unit")});
            }
        }
        found.push_back(std::move(f));
    }

    // slang's warnings, each kept where its file's selection enables it.
    {
        std::vector<Finding> slang;
        JsonWriter fr;
        fr.beginObject().key("diagnostics").beginArray();
        for (const host::LintCompilation::Warning &sw : lc.warnings()) {
            const host::SlangWarningInfo *info = host::slangWarningByName(sw.option);
            if (!info) {
                continue;
            }
            Finding f;
            f.code = host::slangMarkerId(info->num);
            f.severity = "warning";
            f.message = sw.message;
            f.file = absPath(sw.file);
            f.line = sw.line;
            f.col = sw.col;
            fr.beginObject().kv("code", f.code).kv("severity", f.severity).kv("file", f.file)
                .kv("line", f.line).kv("col", f.col).endObject();
            slang.push_back(std::move(f));
        }
        fr.endArray().key("selection").raw(selJson);
        fr.key("stages").beginArray().value("select").endArray().endObject();
        lint::yaml::NodeUP res = slang.empty() ? nullptr
            : parseJson("<finish>", engine.finish(fr.str()));
        if (const lint::yaml::Node *ds = res ? res->get("diagnostics") : nullptr) {
            std::set<std::string> on;
            for (const lint::yaml::NodeUP &d : ds->items) {
                int64_t i = num(d.get(), "index");
                if (i >= 0 && static_cast<size_t>(i) < slang.size()) {
                    on.insert(slang[i].code);
                    found.push_back(slang[i]);
                }
            }
            ran.insert(ran.end(), on.begin(), on.end());
        }
    }

    // [severity], source waivers and the -W policy (finish(), C7.4). The
    // rules' findings were selected per node by the run.
    std::vector<Finding> waived;
    {
        lint::yaml::NodeUP wv = parseJson("<waivers>", engine.collectWaivers(lc.waiverTokens()));
        JsonWriter fr;
        fr.beginObject().key("diagnostics").beginArray();
        for (const Finding &f : found) {
            fr.beginObject().kv("code", f.code).kv("severity", f.severity).kv("file", f.file)
                .kv("line", f.line).kv("col", f.col).endObject();
        }
        fr.endArray().key("selection").raw(selJson);
        fr.key("stages").beginArray().value("severity").value("waive").value("policy")
            .endArray();
        fr.key("waivers");
        writeJson(fr, wv ? wv->get("waivers") : nullptr);
        fr.key("ran").beginArray();
        for (const std::string &id : ran) {
            fr.value(id);
        }
        fr.endArray().endObject();
        lint::yaml::NodeUP res = parseJson("<finish>", engine.finish(fr.str()));
        if (!res || !res->get("ok") || !res->get("ok")->b) {
            std::fprintf(stderr, "svcheck: internal error: the findings did not finish\n");
            return 3;
        }
        std::vector<std::string> reasons;
        if (const lint::yaml::Node *ws = res->get("waivers")) {
            for (const lint::yaml::NodeUP &w : ws->items) {
                reasons.push_back(str(w.get(), "reason"));
            }
        }
        std::vector<Finding> kept;
        for (const lint::yaml::NodeUP &d : res->get("diagnostics")->items) {
            int64_t i = num(d.get(), "index");
            Finding f;
            if (i >= 0 && static_cast<size_t>(i) < found.size()) {
                f = found[i];
                f.originalSeverity = str(d.get(), "original_severity");
            } else {
                // A waiver hygiene marker.
                f.code = str(d.get(), "code");
                f.message = str(d.get(), "message");
                f.file = str(d.get(), "file");
                f.line = num(d.get(), "line");
                f.col = num(d.get(), "col");
            }
            f.severity = str(d.get(), "severity");
            kept.push_back(std::move(f));
        }
        for (const lint::yaml::NodeUP &d : res->get("waived")->items) {
            int64_t i = num(d.get(), "index");
            int64_t wi = num(d.get(), "waiver");
            if (i < 0 || static_cast<size_t>(i) >= found.size()) {
                continue;
            }
            Finding f = found[i];
            f.waived = true;
            f.severity = str(d.get(), "severity");
            if (wi >= 0 && static_cast<size_t>(wi) < reasons.size()) {
                f.waivedBy = reasons[wi];
            }
            waived.push_back(std::move(f));
        }
        found.swap(kept);
    }
    // Tool markers about no place first, then by place.
    auto byPlace = [](const Finding &a, const Finding &b) {
        return std::make_tuple(!a.file.empty(), a.file, a.line, a.col)
            < std::make_tuple(!b.file.empty(), b.file, b.line, b.col);
    };
    std::stable_sort(found.begin(), found.end(), byPlace);
    std::stable_sort(waived.begin(), waived.end(), byPlace);

    rs.totalS = std::chrono::duration<double>(Clock::now() - t0).count();
    rs.heapEnd = host::heapInUse();
    if (opts.format == "json") {
        writeFindingsJson(found, waived, lc, result.get(), opts.stats ? &rs : nullptr);
    } else if (opts.format == "sarif") {
        writeSarif(found, waived, engine, result.get());
    } else if (opts.format == "github") {
        writeGithub(found);
    } else {
        std::vector<Finding> shownFindings = found;
        if (opts.showWaived) {
            shownFindings.insert(shownFindings.end(), waived.begin(), waived.end());
            std::stable_sort(shownFindings.begin(), shownFindings.end(), byPlace);
        }
        writeText(shownFindings);
    }
    if (opts.statistics) {
        printStatistics(found, opts.format == "text" ? stdout : stderr);
    }
    if (opts.stats && opts.format != "json") {
        writeStatsText(rs, lc, result.get());
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
        std::string w = waived.empty() ? "" : ", " + std::to_string(waived.size()) + " waived";
        std::fprintf(stderr, "svcheck: %zu error%s, %zu warning%s%s%s\n", errors,
            errors == 1 ? "" : "s", warnings, warnings == 1 ? "" : "s", w.c_str(),
            compile.c_str());
    }
    return (errors || warnings || lc.errors()) ? 1 : 0;
}

}
}
