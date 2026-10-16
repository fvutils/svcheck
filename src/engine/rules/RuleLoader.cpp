/*
 * RuleLoader.cpp
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
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_set>
#include "Reflect.h"
#include "RuleLoader.h"
#include "../catalog/Catalog.h"
#include "../cel/CelCheck.h"
#include "../cel/CelNode.h"
#include "../cel/CelParser.h"
#include "../yaml/SourceMap.h"

namespace svcheck {
namespace lint {
namespace rules {

size_t codeAt(const std::string &s, size_t i) {
    auto letters = [&s](size_t &j) {
        size_t b = j;
        while (j < s.size() && std::isupper(static_cast<unsigned char>(s[j]))) {
            j++;
        }
        return j - b;
    };
    size_t j = i, n = letters(j);
    if (n < 2 || n > 5) {
        return 0;
    }
    if (j < s.size() && s[j] == '-') {
        j++;
        n = letters(j);
        if (n < 2 || n > 5) {
            return 0;
        }
    }
    for (int k=0; k<3; k++, j++) {
        if (j >= s.size() || !std::isdigit(static_cast<unsigned char>(s[j]))) {
            return 0;
        }
    }
    if (j < s.size() && std::isalnum(static_cast<unsigned char>(s[j]))) {
        return 0;
    }
    return j - i;
}

std::string namespaceOf(const std::string &code) {
    size_t n = code.size();
    while (n && std::isdigit(static_cast<unsigned char>(code[n-1]))) {
        n--;
    }
    return code.substr(0, n);
}

bool isReservedNamespace(const std::string &ns) {
    std::string org = ns.substr(0, ns.find('-'));
    return org == "SVCHK";
}

bool optionValue(const RuleOption &o, const yaml::Node *v, cel::Value &out) {
    using NK = yaml::Node::Kind;
    if (o.type == "string" && v->kind == NK::String) {
        out = cel::Value::string(v->s);
    } else if (o.type == "int" && v->kind == NK::Int) {
        out = cel::Value::integer(v->i);
    } else if (o.type == "bool" && v->kind == NK::Bool) {
        out = cel::Value::boolean(v->b);
    } else if (o.type == "string-list" && v->kind == NK::Seq) {
        cel::ValueList l;
        for (const yaml::NodeUP &i : v->items) {
            if (i->kind != NK::String) {
                return false;
            }
            l.push_back(cel::Value::string(i->s));
        }
        out = cel::Value::mkList(std::move(l));
    } else {
        return false;
    }
    return true;
}

namespace {

using yaml::Node;
using NK = Node::Kind;
namespace rf = svcheck::ast::reflect;

const std::vector<std::string> Statuses = {"active", "deprecated", "retired"};
const std::vector<std::string> Severities = {"error", "warning", "info", "hint"};

// Names that a let: may not take: the variables every rule already has.
const std::vector<std::string> Reserved = {"node", "options", "finding"};

std::string lower(const std::string &s) {
    std::string r(s);
    for (char &c : r) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return r;
}

// Levenshtein distance, case-insensitive.
size_t distance(const std::string &a0, const std::string &b0) {
    std::string a = lower(a0), b = lower(b0);
    std::vector<size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j=0; j<=b.size(); j++) {
        prev[j] = j;
    }
    for (size_t i=1; i<=a.size(); i++) {
        cur[0] = i;
        for (size_t j=1; j<=b.size(); j++) {
            size_t sub = prev[j-1] + (a[i-1] == b[j-1] ? 0 : 1);
            cur[j] = std::min(std::min(prev[j] + 1, cur[j-1] + 1), sub);
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

// The candidate nearest `s`, if it is near enough to be a likely typo.
std::string suggest(const std::string &s, const std::vector<std::string> &cands) {
    std::string best;
    size_t bestd = 0;
    for (const std::string &c : cands) {
        size_t d = distance(s, c);
        if (best.empty() || d < bestd) {
            best = c;
            bestd = d;
        }
    }
    size_t limit = std::max<size_t>(2, s.size() / 3);
    return (!best.empty() && bestd <= limit) ? best : std::string();
}

std::string didYouMean(const std::string &s, const std::vector<std::string> &cands) {
    std::string m = suggest(s, cands);
    return m.empty() ? std::string() : "; did you mean '" + m + "'?";
}

std::string joinList(const std::vector<std::string> &l) {
    std::string r;
    for (size_t i=0; i<l.size(); i++) {
        r += (i ? ", " : "") + l[i];
    }
    return r;
}

// How a message describes what a value is: "a string", "a list".
std::string what(const Node *n) {
    switch (n->kind) {
        case NK::Null: return "null";
        case NK::Bool: return "a bool";
        case NK::Int: return "an integer";
        case NK::Float: return "a number";
        case NK::String: return "a string";
        case NK::Map: return "a mapping";
        case NK::Seq: return "a list";
    }
    return "a value";
}

bool isIdent(const std::string &s) {
    if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) {
        return false;
    }
    for (char c : s) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
            return false;
        }
    }
    return true;
}

// [a-z][a-z0-9]*(-[a-z0-9]+)*, as taxonomy.is_kebab.
bool isKebab(const std::string &s) {
    if (s.empty() || !std::islower(static_cast<unsigned char>(s[0]))) {
        return false;
    }
    for (size_t i=0; i<s.size(); i++) {
        char c = s[i];
        if (c == '-') {
            if (i + 1 >= s.size() || s[i+1] == '-') {
                return false;
            }
        } else if (!(std::islower(static_cast<unsigned char>(c))
                || std::isdigit(static_cast<unsigned char>(c)))) {
            return false;
        }
    }
    return true;
}

// YYYY.N, as taxonomy.RULES_VERSION_RE.
bool isVersion(const std::string &s) {
    size_t dot = s.find('.');
    if (dot != 4 || s.size() < 6) {
        return false;
    }
    for (size_t i=0; i<s.size(); i++) {
        if (i != dot && !std::isdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
    }
    return true;
}

std::string toUpper(std::string s) {
    for (char &c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

bool isXKey(const std::string &k) {
    return k.size() > 2 && k[0] == 'x' && k[1] == '-';
}

const FieldSpec *findField(const std::string &name) {
    for (const FieldSpec &f : ruleFields()) {
        if (name == f.name) {
            return &f;
        }
    }
    return nullptr;
}

std::vector<std::string> classNames() {
    std::vector<std::string> r;
    for (uint16_t k=1; k<=rf::NumClasses; k++) {
        r.push_back(rf::classes[k].name);
    }
    return r;
}

/** One piece of a message template. */
struct Piece {
    bool            expr = false;
    std::string     text;
    uint32_t        offset = 0;     // expr: where its text starts in the template
};

// Splits a message template at its ${...} interpolations. `$${` is a
// literal `${`. The end of an interpolation is the `}` that balances its
// `{`, outside CEL string literals, so `${ {'a': 1}['a'] }` and
// `${ "}" }` work. Returns false, with `err` at `errOff`, on a bad one.
bool splitTemplate(const std::string &s, std::vector<Piece> &out, uint32_t &errOff,
        std::string &err) {
    std::string lit;
    size_t i = 0, n = s.size();
    while (i < n) {
        if (s.compare(i, 3, "$${") == 0) {
            lit += "${";
            i += 3;
            continue;
        }
        if (s.compare(i, 2, "${") != 0) {
            lit += s[i++];
            continue;
        }
        size_t j = i + 2;
        int depth = 0;
        bool closed = false;
        while (j < n) {
            char c = s[j];
            if (c == '\'' || c == '"') {
                bool tri = (j + 2 < n && s[j+1] == c && s[j+2] == c);
                j += tri ? 3 : 1;
                while (j < n) {
                    if (s[j] == '\\') {
                        j += 2;
                    } else if (tri ? (s.compare(j, 3, std::string(3, c)) == 0) : (s[j] == c)) {
                        j += tri ? 3 : 1;
                        break;
                    } else if (!tri && s[j] == '\n') {
                        break;
                    } else {
                        j++;
                    }
                }
                continue;
            }
            if (c == '{') {
                depth++;
            } else if (c == '}') {
                if (depth == 0) {
                    closed = true;
                    break;
                }
                depth--;
            }
            j++;
        }
        if (!closed) {
            errOff = static_cast<uint32_t>(i);
            err = "unterminated '${' in the message (write '$${' for a literal '${')";
            return false;
        }
        std::string text = s.substr(i + 2, j - (i + 2));
        if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
            errOff = static_cast<uint32_t>(i);
            err = "empty '${}' in the message";
            return false;
        }
        if (!lit.empty()) {
            out.push_back({false, lit, 0});
            lit.clear();
        }
        out.push_back({true, text, static_cast<uint32_t>(i + 2)});
        i = j + 1;
    }
    if (!lit.empty()) {
        out.push_back({false, lit, 0});
    }
    return true;
}

// `rel`, a path a file named `from` gives, as a path from where `from` is:
// relative to its directory, '/' separated, without '.' or '..' steps.
std::string resolvePath(const std::string &from, const std::string &rel) {
    std::string p = rel;
    if (!(rel.size() && rel[0] == '/')) {
        size_t slash = from.find_last_of('/');
        if (slash != std::string::npos) {
            p = from.substr(0, slash + 1) + rel;
        }
    }
    std::vector<std::string> steps;
    size_t i = 0;
    while (i <= p.size()) {
        size_t j = p.find('/', i);
        if (j == std::string::npos) {
            j = p.size();
        }
        std::string step = p.substr(i, j - i);
        if (step == "..") {
            if (!steps.empty() && steps.back() != "..") {
                steps.pop_back();
            } else if (!(p.size() && p[0] == '/')) {
                steps.push_back(step);
            }
        } else if (!step.empty() && step != ".") {
            steps.push_back(step);
        }
        i = j + 1;
    }
    std::string out = (p.size() && p[0] == '/') ? "/" : "";
    for (size_t k=0; k<steps.size(); k++) {
        out += (k ? "/" : "") + steps[k];
    }
    return out;
}

class Loader {
public:
    Loader(const LintSource &src, bool builtin, RuleClaims &claims,
            std::vector<RuleDiagnostic> &diags, const FileReader &reader) :
            m_src(src), m_builtin(builtin), m_claims(claims), m_diags(diags), m_reader(reader) {
        addFile(src.name, src.text);
    }

    RuleSetUP load();

private:
    //************************************************************************
    //* Diagnostics
    //************************************************************************

    void diag(const char *code, const yaml::Mark &m, const std::string &msg,
            RuleDiagnostic::Severity sev=RuleDiagnostic::Severity::Error) {
        diagIn(m_cur, code, m, msg, sev);
    }

    // A diagnostic at mark `m` of file `file` (m_files).
    void diagIn(size_t file, const char *code, const yaml::Mark &m, const std::string &msg,
            RuleDiagnostic::Severity sev=RuleDiagnostic::Severity::Error) {
        RuleDiagnostic d;
        d.code = code;
        d.severity = sev;
        d.message = msg;
        d.file = m_files[file]->name;
        d.line = m.line;
        d.col = m.col;
        d.rule = m_rule;
        m_diags.push_back(std::move(d));
        if (sev == RuleDiagnostic::Severity::Error) {
            m_errors++;
        }
    }

    // A CEL error, at its position in the rule file. The same error found
    // twice (a file-level helper's, from two rules) is reported once.
    void celDiag(const char *code, uint32_t source, uint32_t offset, const std::string &msg) {
        if (!m_reported.insert(std::make_tuple(source, offset, msg)).second) {
            return;
        }
        const Source &s = m_sources[source];
        diagIn(s.file, code, m_files[s.file]->lines->at(yaml::sourceOffset(*s.node,
            s.base + offset)), msg);
    }

    void checkErrors(const std::vector<cel::CheckError> &errs) {
        for (const cel::CheckError &e : errs) {
            const char *c = (e.kind == cel::CheckError::Kind::Profile) ? code::ExprSyntax
                : (e.kind == cel::CheckError::Kind::Limit) ? code::ExprLimit : code::ExprType;
            celDiag(c, e.source, e.offset, e.message);
        }
    }

    // A mark inside string node `n`: byte `off` of its value.
    yaml::Mark markIn(const Node *n, uint32_t off) {
        return m_files[m_cur]->lines->at(yaml::sourceOffset(*n, off));
    }

    //************************************************************************
    //* CEL
    //************************************************************************

    struct Source {
        const Node      *node;
        uint32_t        base;       // where the expression starts in the node's value
        size_t          file;       // m_files
    };

    // Parses `text`, which starts at byte `base` of string node `n`. Null on
    // a syntax error, which is reported.
    cel::ExprUP parseCel(const Node *n, uint32_t base, const std::string &text, uint32_t &source) {
        source = static_cast<uint32_t>(m_sources.size());
        m_sources.push_back({n, base, m_cur});
        try {
            cel::CelParser p(text);
            return p.parse();
        } catch (const cel::SyntaxError &e) {
            celDiag(code::ExprSyntax, source, e.offset(), e.what());
            return nullptr;
        }
    }

    // A field whose value is one CEL expression: it must be a string.
    bool celString(const Node *n, const std::string &field) {
        if (n->kind == NK::String) {
            return true;
        }
        diag(code::FieldInvalid, n->mark, "'" + field + "' must be a CEL expression written as "
            "a string, not " + what(n) + "; quote it, or use a block scalar (>-)");
        return false;
    }

    // Parses the helpers of a define: mapping into `out`, keeping the parsed
    // bodies alive for the loader's lifetime.
    void parseDefines(const Node *map, std::vector<cel::Define> &out,
            std::vector<const Node::Member *> *members) {
        if (map->kind != NK::Map) {
            diag(code::FieldInvalid, map->mark, "'define' must be a mapping of helpers "
                "(name: expression, or name(params): expression), not " + what(map));
            return;
        }
        for (const Node::Member &m : map->members) {
            cel::Define d;
            std::string err;
            if (!cel::parseDefineHead(m.key, d, err)) {
                diag(code::FieldInvalid, m.keyMark, err);
                continue;
            }
            if (!celString(m.value.get(), "define: " + m.key)) {
                out.push_back(d);       // body null: a use fails quietly
                if (members) {
                    members->push_back(&m);
                }
                continue;
            }
            uint32_t src;
            cel::ExprUP body = parseCel(m.value.get(), 0, m.value->s, src);
            // Kept even when it did not parse, so that a use fails quietly
            // rather than as an unknown function.
            d.body = body.get();
            d.source = src;
            if (body) {
                m_parsed.push_back(std::move(body));
            }
            out.push_back(d);
            if (members) {
                members->push_back(&m);
            }
        }
    }

    //************************************************************************
    //* Fields
    //************************************************************************

    // Checks a scalar-valued field (format §3.1). Reports and returns false
    // when the value does not fit.
    bool validScalar(const FieldSpec &f, const Node *v) {
        std::string name(f.name);
        switch (f.type) {
            case FieldType::String:
            case FieldType::Template:
                if (v->kind != NK::String) {
                    diag(code::FieldInvalid, v->mark, "'" + name + "' must be a string, not "
                        + what(v));
                    return false;
                }
                return true;
            case FieldType::Line:
                if (v->kind != NK::String) {
                    diag(code::FieldInvalid, v->mark, "'" + name + "' must be a string, not "
                        + what(v));
                    return false;
                }
                if (v->s.find("${") != std::string::npos) {
                    diag(code::FieldInvalid, markIn(v, static_cast<uint32_t>(v->s.find("${"))),
                        "'" + name + "' is not interpolated; '${' belongs in 'message'");
                    return false;
                }
                if (v->s.find('\n') != std::string::npos
                        && v->s.find('\n') + 1 < v->s.size()) {
                    diag(code::FieldInvalid, v->mark, "'" + name + "' must be one line");
                    return false;
                }
                return true;
            case FieldType::Kebab:
                if (v->kind != NK::String || !isKebab(v->s)) {
                    diag(code::FieldInvalid, v->mark, "'" + name + "' must be a kebab-case "
                        "name (lower-case words joined by '-', as 'init-list-duplicate-target'), "
                        "not " + (v->kind == NK::String ? "'" + v->s + "'" : what(v)));
                    return false;
                }
                return true;
            case FieldType::Enum:
                if (v->kind != NK::String) {
                    diag(code::FieldInvalid, v->mark, "'" + name + "' must be one of "
                        + joinList(f.values) + ", not " + what(v));
                    return false;
                }
                if (std::find(f.values.begin(), f.values.end(), v->s) == f.values.end()) {
                    diag(code::FieldInvalid, v->mark, "'" + v->s + "' is not a valid '" + name
                        + "'; it must be one of " + joinList(f.values)
                        + didYouMean(v->s, f.values));
                    return false;
                }
                return true;
            case FieldType::StringList:
                if (v->kind != NK::Seq) {
                    diag(code::FieldInvalid, v->mark, "'" + name + "' must be a list of strings, "
                        "not " + what(v));
                    return false;
                }
                for (const yaml::NodeUP &i : v->items) {
                    if (i->kind != NK::String) {
                        diag(code::FieldInvalid, i->mark, "'" + name + "' must be a list of "
                            "strings, but this item is " + what(i.get()));
                        return false;
                    }
                }
                return true;
            case FieldType::Code:
                return validCode(name, v, true);
            case FieldType::CodeList:
                if (v->kind != NK::Seq) {
                    diag(code::FieldInvalid, v->mark, "'" + name + "' must be a list of marker "
                        "IDs, not " + what(v));
                    return false;
                }
                for (const yaml::NodeUP &i : v->items) {
                    // An alias is an ID the rule answers to; replaced_by
                    // names another rule, which may be anyone's.
                    if (!validCode(name, i.get(), name == "aliases")) {
                        return false;
                    }
                }
                return true;
            case FieldType::Bool:
                if (v->kind != NK::Bool) {
                    diag(code::FieldInvalid, v->mark, "'" + name + "' must be true or false, not "
                        + (v->kind == NK::String ? "the string '" + v->s + "'" : what(v)));
                    return false;
                }
                return true;
            case FieldType::Version:
                if (v->kind == NK::Int || v->kind == NK::Float) {
                    diag(code::FieldInvalid, v->mark, "'" + name + "' must be a string: quote "
                        "it, as \"" + v->s + "\" (unquoted, it is a number, and 2026.10 would "
                        "read as 2026.1)");
                    return false;
                }
                if (v->kind != NK::String || !isVersion(v->s)) {
                    diag(code::FieldInvalid, v->mark, "'" + name + "' must be a rules version, "
                        "YYYY.N (\"2026.1\"), not " + (v->kind == NK::String ? "'" + v->s + "'"
                        : what(v)));
                    return false;
                }
                return true;
            default:
                return true;
        }
    }

    // Checks a marker ID: its grammar (format §5.1), then, for an ID the rule
    // owns, its namespace (format §5).
    bool validCode(const std::string &field, const Node *v, bool owned) {
        if (v->kind != NK::String) {
            diag(code::FieldInvalid, v->mark, "'" + field + "' must be a marker ID, as "
                "ACME001, not " + what(v));
            return false;
        }
        const std::string &s = v->s;
        if (s.empty() || codeAt(s, 0) != s.size()) {
            size_t digits = s.size() - namespaceOf(s).size();
            std::string hint;
            if (digits > 3) {
                hint = "; a namespace is letters only, so '" + s.substr(0, s.size() - 3)
                    + "' is not one (name a team with letters, as ACME-NET)";
            } else if (s != toUpper(s)) {
                hint = "; IDs are upper case";
            }
            diag(code::FieldInvalid, v->mark, "'" + s + "' is not a marker ID: '" + field
                + "' must be a namespace of 2-5 capital letters, optionally followed by '-' "
                "and a team of 2-5 more, then 3 digits, as ACME001 or ACME-NET001" + hint);
            return false;
        }
        if (!owned) {
            return true;
        }
        std::string ns = namespaceOf(s);
        if (m_builtin && ns != "SVCHK") {
            diag(code::IdConflict, v->mark, "'" + s + "' is in namespace '" + ns + "'; a "
                "built-in rule's IDs are in the SVCHK namespace");
            return false;
        }
        if (!m_builtin && isReservedNamespace(ns)) {
            diag(code::IdConflict, v->mark, "'" + s + "' is in the reserved namespace '" + ns
                + "': SVCHK belongs to svcheck, with or without a team "
                "qualifier; choose a namespace of your own, as ACME001");
            return false;
        }
        return true;
    }

    // Claims the rule's ID, aliases and name across every loaded file
    // (format §5). Claims nothing, and returns false, on a clash.
    bool claim(const Rule &r, std::map<std::string, const Node *> &val) {
        struct Want { std::string key; const Node *at; bool name; std::string what; };
        std::vector<Want> want;
        if (!r.m_id.empty()) {
            want.push_back({r.m_id, val["id"], false, "ID"});
        }
        if (!r.m_name.empty()) {
            want.push_back({r.m_name, val["name"], true, "name"});
        }
        for (size_t i=0; i<r.m_aliases.size(); i++) {
            want.push_back({r.m_aliases[i], val["aliases"]->items[i].get(), false, "alias"});
        }
        bool ok = true;
        std::set<std::string> mine;
        for (const Want &w : want) {
            std::map<std::string, RuleClaim> &table = w.name ? m_claims.names : m_claims.ids;
            auto it = table.find(w.key);
            if (it != table.end()) {
                const RuleClaim &c = it->second;
                std::string owner = c.what == "ID" ? "the ID of a rule"
                    : c.what == "alias" ? "an alias of rule '" + c.rule + "'"
                    : "the name of rule '" + c.rule + "'";
                diag(code::IdConflict, w.at->mark, "rule " + w.what + " '" + w.key + "' is "
                    "already " + owner + ", at " + c.file + ":" + std::to_string(c.mark.line)
                    + ":" + std::to_string(c.mark.col));
                RuleDiagnostic &d = m_diags.back();
                d.hasRelated = true;
                d.relatedFile = c.file;
                d.relatedLine = c.mark.line;
                d.relatedCol = c.mark.col;
                ok = false;
            } else if (!mine.insert((w.name ? "n:" : "i:") + w.key).second) {
                diag(code::IdConflict, w.at->mark, "rule " + w.what + " '" + w.key + "' "
                    "repeats one of this rule's own IDs");
                ok = false;
            }
        }
        if (ok) {
            for (const Want &w : want) {
                RuleClaim c;
                c.file = m_src.name;
                c.mark = w.at->mark;
                c.rule = r.m_id.empty() ? r.m_name : r.m_id;
                c.what = w.what;
                (w.name ? m_claims.names : m_claims.ids)[w.key] = c;
            }
        }
        return ok;
    }

    void setScalar(Rule &r, const std::string &name, const Node *v) {
        auto list = [v]() {
            std::vector<std::string> l;
            for (const yaml::NodeUP &i : v->items) {
                l.push_back(i->s);
            }
            return l;
        };
        if (name == "id") r.m_id = v->s;
        else if (name == "name") r.m_name = v->s;
        else if (name == "tier") r.m_tier = v->s;
        else if (name == "family") r.m_family = v->s;
        else if (name == "summary") r.m_summary = v->s;
        else if (name == "message") r.m_message = v->s;
        else if (name == "doc") r.m_doc = v->s;
        else if (name == "lrm") r.m_lrm = list();
        else if (name == "since") r.m_since = v->s;
        else if (name == "status") r.m_status = v->s;
        else if (name == "aliases") r.m_aliases = list();
        else if (name == "replaced_by") r.m_replacedBy = list();
        else if (name == "severity") r.m_severity = v->s;
        else if (name == "default_enabled") r.m_defaultEnabled = v->b;
    }

    // Strict keys: reports each key of mapping `n` not in `allowed` (x-*
    // keys are the author's and allowed everywhere under sv-rules).
    void checkKeys(const Node *n, const std::vector<std::string> &allowed, const std::string &in) {
        for (const Node::Member &m : n->members) {
            if (isXKey(m.key)
                    || std::find(allowed.begin(), allowed.end(), m.key) != allowed.end()) {
                continue;
            }
            diag(code::UnknownKey, m.keyMark, "unknown key '" + m.key + "' " + in
                + didYouMean(m.key, allowed));
        }
    }

    //************************************************************************
    //* File level
    //************************************************************************

    void loadDefaults(const Node *d) {
        if (d->kind != NK::Map) {
            diag(code::FieldInvalid, d->mark, "'defaults' must be a mapping of rule fields, not "
                + what(d));
            return;
        }
        std::vector<std::string> defaultable;
        for (const FieldSpec &f : ruleFields()) {
            if (f.defaultable) {
                defaultable.push_back(f.name);
            }
        }
        for (const Node::Member &m : d->members) {
            if (isXKey(m.key)) {
                continue;
            }
            const FieldSpec *f = findField(m.key);
            if (!f) {
                diag(code::UnknownKey, m.keyMark, "unknown key '" + m.key + "' in 'defaults'"
                    + didYouMean(m.key, defaultable));
            } else if (!f->defaultable) {
                diag(code::FieldInvalid, m.keyMark, "'" + m.key + "' cannot be set in "
                    "'defaults'; only " + joinList(defaultable) + " can");
            } else if (validScalar(*f, m.value.get())) {
                m_defaults[m.key] = m.value.get();
            } else {
                m_badDefaults.insert(m.key);
            }
        }
    }

    //************************************************************************
    //* Rules
    //************************************************************************

    bool loadKinds(const Node *n, const std::string &field, KindSet &out) {
        std::vector<const Node *> names;
        if (n->kind == NK::String) {
            names.push_back(n);
        } else if (n->kind == NK::Seq && !n->items.empty()) {
            for (const yaml::NodeUP &i : n->items) {
                names.push_back(i.get());
            }
        } else {
            diag(code::FieldInvalid, n->mark, "'" + field + "' must be a node kind or a "
                "non-empty list of them, not " + (n->kind == NK::Seq ? std::string("an empty list")
                : what(n)));
            return false;
        }
        bool ok = true;
        for (const Node *k : names) {
            uint16_t cls;
            if (k->kind != NK::String) {
                diag(code::FieldInvalid, k->mark, "'" + field + "' must name node kinds, but "
                    "this item is " + what(k));
                ok = false;
            } else if (!cel::lookupClass(k->s, cls)) {
                if (m_classNames.empty()) {
                    m_classNames = classNames();
                }
                diag(code::UnknownKind, k->mark, "unknown node kind '" + k->s + "' in '" + field
                    + "'" + didYouMean(k->s, m_classNames));
                ok = false;
            } else if (std::find(out.classes.begin(), out.classes.end(), cls) == out.classes.end()) {
                out.names.push_back(k->s);
                out.classes.push_back(cls);
            }
        }
        return ok;
    }

    bool loadAncestor(const Node *n, const std::string &field, AncestorTest &out) {
        if (n->kind != NK::Map) {
            diag(code::FieldInvalid, n->mark, "'" + field + "' must be a mapping, as { kind: "
                "Action }, not " + what(n));
            return false;
        }
        checkKeys(n, {"kind", "stop"}, "in '" + field + "'");
        bool ok = true;
        out.present = true;
        if (const Node *k = n->get("kind")) {
            ok &= loadKinds(k, field + ".kind", out.kind);
        } else {
            diag(code::FieldInvalid, n->mark, "'" + field + "' has no 'kind'");
            ok = false;
        }
        if (const Node *s = n->get("stop")) {
            ok &= loadKinds(s, field + ".stop", out.stop);
        }
        return ok;
    }

    bool loadMatch(const Node *n, Rule &r) {
        if (n->kind != NK::Map) {
            diag(code::FieldInvalid, n->mark, "'match' must be a mapping, as { kind: ExprBin }, "
                "not " + what(n));
            return false;
        }
        checkKeys(n, {"kind", "inside", "not_inside"}, "in 'match'");
        bool ok = true;
        if (const Node *k = n->get("kind")) {
            ok &= loadKinds(k, "match.kind", r.m_match);
        } else {
            diag(code::FieldInvalid, n->mark, "'match' has no 'kind'");
            ok = false;
        }
        if (const Node *i = n->get("inside")) {
            ok &= loadAncestor(i, "inside", r.m_inside);
        }
        if (const Node *i = n->get("not_inside")) {
            ok &= loadAncestor(i, "not_inside", r.m_notInside);
        }
        return ok;
    }

    bool loadOptions(const Node *n, std::vector<RuleOption> &out) {
        if (n->kind != NK::Map) {
            diag(code::FieldInvalid, n->mark, "'options' must be a mapping of option names to "
                "specs, not " + what(n));
            return false;
        }
        bool ok = true;
        for (const Node::Member &m : n->members) {
            const Node *spec = m.value.get();
            RuleOption o;
            o.name = m.key;
            if (!isIdent(m.key)) {
                diag(code::FieldInvalid, m.keyMark, "option name '" + m.key + "' must be an "
                    "identifier (letters, digits and '_'), since expressions read it as "
                    "options." + m.key);
                ok = false;
                continue;
            }
            if (spec->kind != NK::Map) {
                diag(code::FieldInvalid, spec->mark, "option '" + m.key + "' must be a mapping "
                    "(type, default, help, choices), not " + what(spec));
                ok = false;
                continue;
            }
            size_t before = m_errors;
            checkKeys(spec, {"type", "default", "help", "choices"}, "in option '" + m.key + "'");
            o.type = "string";
            if (const Node *t = spec->get("type")) {
                const std::vector<std::string> &types = optionTypes();
                if (t->kind != NK::String
                        || std::find(types.begin(), types.end(), t->s) == types.end()) {
                    diag(code::FieldInvalid, t->mark, "option type must be one of "
                        + joinList(types) + ", not " + (t->kind == NK::String ? "'" + t->s + "'"
                        : what(t)) + (t->kind == NK::String ? didYouMean(t->s, types) : ""));
                    ok = false;
                    continue;
                }
                o.type = t->s;
            }
            o.celType = (o.type == "string") ? cel::Type(cel::Type::Kind::String)
                : (o.type == "int") ? cel::Type(cel::Type::Kind::Int)
                : (o.type == "bool") ? cel::Type(cel::Type::Kind::Bool)
                : cel::Type::list(cel::Type(cel::Type::Kind::String));
            if (const Node *h = spec->get("help")) {
                if (h->kind != NK::String) {
                    diag(code::FieldInvalid, h->mark, "option 'help' must be a string, not "
                        + what(h));
                } else {
                    o.help = h->s;
                }
            }
            if (const Node *c = spec->get("choices")) {
                if (c->kind != NK::Seq || c->items.empty()) {
                    diag(code::FieldInvalid, c->mark, "option 'choices' must be a non-empty "
                        "list, not " + (c->kind == NK::Seq ? std::string("an empty list")
                        : what(c)));
                }
                for (const yaml::NodeUP &i : c->items) {
                    cel::Value v;
                    if (!rules::optionValue(o, i.get(), v)) {
                        diag(code::FieldInvalid, i->mark, "option '" + m.key + "' is of type "
                            + o.type + ", but this choice is " + what(i.get()));
                    } else {
                        o.choices.push_back(v);
                    }
                }
            }
            const Node *d = spec->get("default");
            if (!d) {
                diag(code::FieldInvalid, spec->mark, "option '" + m.key + "' has no 'default'; "
                    "a rule's expressions need a value for every option");
            } else if (!rules::optionValue(o, d, o.def)) {
                diag(code::FieldInvalid, d->mark, "option '" + m.key + "' is of type " + o.type
                    + ", but its default is " + what(d));
            } else if (!o.choices.empty()) {
                bool found = false;
                for (const cel::Value &c : o.choices) {
                    found |= (c.kind == o.def.kind && c.s == o.def.s && c.i == o.def.i
                        && c.b == o.def.b);
                }
                if (!found) {
                    diag(code::FieldInvalid, d->mark, "the default of option '" + m.key
                        + "' is not one of its choices");
                }
            }
            if (m_errors != before) {
                ok = false;
                continue;
            }
            out.push_back(std::move(o));
        }
        return ok;
    }

    //************************************************************************
    //* uses: (svcheck-plan P1.4)
    //************************************************************************

    size_t addFile(const std::string &name, const std::string &text) {
        std::unique_ptr<File> f(new File());
        f->name = name;
        f->text = text;
        f->lines.reset(new yaml::LineIndex(f->text));
        m_files.push_back(std::move(f));
        return m_files.size() - 1;
    }

    // The files a `uses:` list of file m_cur names, each loaded once, before
    // what names it: their helpers and options join the rule file's.
    void loadUses(const Node *list) {
        if (list->kind != NK::Seq) {
            diag(code::FieldInvalid, list->mark, "'uses' must be a list of file paths, not "
                + what(list));
            return;
        }
        for (const yaml::NodeUP &item : list->items) {
            if (item->kind != NK::String || item->s.empty()) {
                diag(code::FieldInvalid, item->mark, "a 'uses' entry must be a file path, not "
                    + what(item.get()));
                continue;
            }
            std::string path = resolvePath(m_files[m_cur]->name, item->s);
            auto on = std::find(m_useStack.begin(), m_useStack.end(), path);
            if (on != m_useStack.end()) {
                std::string chain;
                for (auto it=on; it!=m_useStack.end(); it++) {
                    chain += *it + " -> ";
                }
                diag(code::FileStructure, item->mark, "'uses' makes a cycle: " + chain + path);
                continue;
            }
            if (!m_usedPaths.insert(path).second) {
                continue;
            }
            std::string text;
            if (!m_reader) {
                diag(code::FileStructure, item->mark, "cannot load '" + item->s + "': this "
                    "host reads no files for 'uses'");
                continue;
            }
            if (!m_reader(path, text)) {
                diag(code::FileStructure, item->mark, "cannot read '" + path + "', which "
                    "'uses' names");
                continue;
            }
            loadHelperFile(path, text);
        }
    }

    // A file named in `uses:`: helpers, options and uses only.
    void loadHelperFile(const std::string &path, const std::string &text) {
        size_t saved = m_cur;
        m_cur = addFile(path, text);
        m_useStack.push_back(path);
        File &f = *m_files[m_cur];
        yaml::Mark start;
        start.line = start.col = 1;
        const Node *top = nullptr;
        try {
            f.root = yaml::parse(path, f.text);
        } catch (const yaml::YamlError &e) {
            diag(code::FileYaml, e.mark(), e.what());
        }
        if (f.root && f.root->kind == NK::Map) {
            top = f.root->get("sv-rules");
        }
        if (!f.root || (f.root->kind != NK::Map) || !top || top->kind != NK::Map) {
            if (f.root || m_diags.empty() || m_diags.back().code != std::string(code::FileYaml)) {
                diag(code::FileStructure, start, "not a helper file: a file named in 'uses' is "
                    "a mapping with the single key 'sv-rules', holding 'define', 'options', "
                    "'contexts' or 'uses'");
            }
        } else {
            if (const Node *v = top->get("version")) {
                if (v->kind != NK::Int || v->i != 1) {
                    diag(code::FileStructure, v->mark, "this svcheck reads sv-rules version 1");
                }
            }
            for (const Node::Member &m : top->members) {
                if (m.key == "rules" || m.key == "defaults") {
                    diag(code::FileStructure, m.keyMark, "'" + m.key + "' in a file named in "
                        "'uses': such a file holds helpers only ('define', 'options', "
                        "'contexts', 'uses'); load it as a rule file for its rules");
                }
            }
            checkKeys(top, {"version", "define", "options", "uses", "contexts", "rules",
                "defaults"}, "under 'sv-rules' of a file named in 'uses'");
            if (const Node *u = top->get("uses")) {
                loadUses(u);
            }
            if (const Node *o = top->get("options")) {
                std::vector<RuleOption> opts;
                loadOptions(o, opts);
                addFileOptions(o, opts);
            }
            if (const Node *d = top->get("define")) {
                std::vector<cel::Define> defs;
                std::vector<const Node::Member *> keys;
                parseDefines(d, defs, &keys);
                for (size_t i=0; i<defs.size(); i++) {
                    bool clash = false;
                    for (size_t j=0; j<m_usedDefines.size(); j++) {
                        if (m_usedDefines[j].name == defs[i].name) {
                            diag(code::FieldInvalid, keys[i]->keyMark, "helper '" + defs[i].name
                                + "' is also defined in " + m_files[m_usedDefineFile[j]]->name
                                + ", which 'uses' also loads; rename one");
                            clash = true;
                        }
                    }
                    if (!clash) {
                        m_usedDefines.push_back(defs[i]);
                        m_usedDefineKeys.push_back(keys[i]);
                        m_usedDefineFile.push_back(m_cur);
                    }
                }
            }
            if (const Node *c = top->get("contexts")) {
                loadContexts(c, m_usedDefines, m_usedClassifiers);
            }
        }
        m_useStack.pop_back();
        m_cur = saved;
    }

    //************************************************************************
    //* contexts: (sv-contexts.md §3.3; svcheck-plan P1.4)
    //************************************************************************

    // The classifiers of a `contexts:` list in file m_cur, checked with
    // `defines` and the file options loaded so far.
    void loadContexts(const Node *list, const std::vector<cel::Define> &defines,
            std::vector<ClassifierUP> &out) {
        if (list->kind != NK::Seq) {
            diag(code::FieldInvalid, list->mark, "'contexts' must be a list of classifiers, "
                "not " + what(list));
            return;
        }
        for (const yaml::NodeUP &item : list->items) {
            const Node *n = item.get();
            if (n->kind != NK::Map) {
                diag(code::FieldInvalid, n->mark, "a classifier must be a mapping (label, axis, "
                    "carrier, when, ...), not " + what(n));
                continue;
            }
            size_t before = m_errors;
            checkKeys(n, {"label", "axis", "carrier", "strength", "name", "when", "joins"},
                "in a classifier");
            ClassifierUP c(new Classifier());
            c->file = m_files[m_cur]->name;
            c->mark = n->mark;
            for (const char *k : {"label", "axis"}) {
                const Node *v = n->get(k);
                if (!v || v->kind != NK::String || v->s.empty()) {
                    diag(code::FieldInvalid, v ? v->mark : n->mark, std::string("a classifier "
                        "needs a '") + k + "', a non-empty string");
                    continue;
                }
                (std::string(k) == "label" ? c->label : c->axis) = v->s;
            }
            if (const Node *v = n->get("strength")) {
                if (v->kind != NK::String || (v->s != "strong" && v->s != "weak")) {
                    diag(code::FieldInvalid, v->mark, "a classifier's 'strength' is 'strong' "
                        "(content) or 'weak' (a convention), not " + what(v));
                } else {
                    c->strong = (v->s == "strong");
                }
            }
            cel::Type carrierT = cel::Type::node(0);
            if (const Node *v = n->get("carrier")) {
                if (loadKinds(v, "carrier", c->carrier)) {
                    uint16_t base = c->carrier.classes[0];
                    for (uint16_t k : c->carrier.classes) {
                        base = cel::commonBase(base, k);
                    }
                    carrierT = cel::Type::node(base);
                }
            } else {
                diag(code::FieldInvalid, n->mark, "a classifier needs a 'carrier': the node "
                    "kinds it labels (Package, Definition, Class)");
            }
            cel::CheckEnv env;
            env.vars.emplace_back("carrier", carrierT);
            for (const RuleOption &o : m_fileOptions) {
                env.vars.emplace_back("options." + o.name, o.celType);
            }
            env.defines = defines;
            c->options = m_fileOptions;
            auto field = [&](const char *key, const cel::Type &want, cel::ExprUP &e) {
                const Node *v = n->get(key);
                if (!v || !celString(v, key)) {
                    return;
                }
                uint32_t src;
                cel::ExprUP parsed = parseCel(v, 0, v->s, src);
                if (!parsed) {
                    return;
                }
                cel::CelChecker chk(env);
                cel::CheckResult res = chk.check(parsed.get(), src, &want, key);
                checkErrors(res.errors);
                e = std::move(res.expr);
            };
            if (!n->get("when")) {
                diag(code::FieldInvalid, n->mark, "a classifier needs a 'when': a CEL condition "
                    "on `carrier`");
            }
            field("when", cel::Type(cel::Type::Kind::Bool), c->when);
            field("name", cel::Type(cel::Type::Kind::String), c->name);
            field("joins", cel::Type::list(cel::Type::node(0)), c->joins);
            if (m_errors == before && c->when) {
                out.push_back(std::move(c));
            }
        }
    }

    // File-level options (`options:` under `sv-rules`) from file m_cur: every
    // rule of the rule file gets them, as if it declared them.
    void addFileOptions(const Node *map, std::vector<RuleOption> &opts) {
        for (RuleOption &o : opts) {
            bool clash = false;
            for (size_t j=0; j<m_fileOptions.size(); j++) {
                if (m_fileOptions[j].name == o.name) {
                    const Node::Member *k = nullptr;
                    for (const Node::Member &m : map->members) {
                        if (m.key == o.name) {
                            k = &m;
                        }
                    }
                    diag(code::FieldInvalid, k ? k->keyMark : map->mark, "option '" + o.name
                        + "' is also a file option of " + m_files[m_fileOptionFile[j]]->name
                        + "; rename one");
                    clash = true;
                }
            }
            if (!clash) {
                m_fileOptions.push_back(std::move(o));
                m_fileOptionFile.push_back(m_cur);
            }
        }
    }

    // One `// expect:` or `/* expect: */` comment's items, at `begin` in
    // `text` (string node `n`'s value). `line` is the comment's line in the
    // example.
    void parseExpectItems(const Node *n, const std::string &text, size_t begin, size_t end,
            uint32_t line, const Rule &r, const std::string &ex, ExampleFile &f) {
        size_t i = begin;
        bool none = false, any = false;
        while (i < end) {
            size_t comma = text.find(',', i);
            size_t e = (comma == std::string::npos || comma > end) ? end : comma;
            size_t b = text.find_first_not_of(" \t", i);
            size_t last = text.find_last_not_of(" \t\r", e == 0 ? 0 : e - 1);
            if (b >= e || last == std::string::npos || last < b) {
                if (comma == std::string::npos || comma > end) {
                    break;
                }
                i = e + 1;
                continue;
            }
            std::string item = text.substr(b, last + 1 - b);
            yaml::Mark at = markIn(n, static_cast<uint32_t>(b));
            Expectation x;
            size_t len = codeAt(item, 0);
            bool good = false;
            if (item == "none") {
                none = good = true;
            } else if (len) {
                x.id = item.substr(0, len);
                x.line = line;
                size_t k = item.find_first_not_of(" \t", len);
                if (k == std::string::npos) {
                    good = true;
                } else if (item[k] == '@') {
                    unsigned l = 0, c = 0;
                    char tail;
                    std::string pos = item.substr(k + 1);
                    if (sscanf(pos.c_str(), " %u : %u %c", &l, &c, &tail) == 2 && l && c) {
                        x.line = l;
                        x.col = c;
                        good = true;
                    }
                }
            }
            if (!good) {
                diag(code::FieldInvalid, at, "malformed expectation '" + item + "' in example '"
                    + ex + "'; write '// expect: " + (r.m_id.empty() ? "ID" : r.m_id)
                    + "', optionally with '@ line:col'");
            } else if (!x.id.empty()) {
                any = true;
                // (A rule whose own ID was refused is already reported.)
                if (x.id != r.m_id && !r.m_id.empty()) {
                    diag(code::FieldInvalid, at, "example '" + ex + "' expects " + x.id
                        + ", but an example runs only its own rule, " + r.m_id);
                } else {
                    f.expect.push_back(x);
                }
            }
            if (comma == std::string::npos || comma > end) {
                break;
            }
            i = e + 1;
        }
        if (none && any) {
            diag(code::FieldInvalid, markIn(n, static_cast<uint32_t>(begin)), "'expect: none' "
                "together with expected findings, in example '" + ex + "'");
        }
    }

    void parseExpectations(const Node *n, const Rule &r, const std::string &ex, ExampleFile &f) {
        const std::string &t = f.text;
        uint32_t line = 1;
        size_t ls = 0;
        while (ls <= t.size()) {
            size_t le = t.find('\n', ls);
            if (le == std::string::npos) {
                le = t.size();
            }
            for (size_t p = t.find("expect:", ls); p != std::string::npos && p < le;
                    p = t.find("expect:", p + 1)) {
                size_t q = p;
                while (q > ls && (t[q-1] == ' ' || t[q-1] == '\t')) {
                    q--;
                }
                if (q < ls + 2 || t[q-2] != '/' || (t[q-1] != '/' && t[q-1] != '*')) {
                    continue;
                }
                size_t end = le;
                if (t[q-1] == '*') {
                    size_t close = t.find("*/", p);
                    if (close != std::string::npos && close < le) {
                        end = close;
                    }
                }
                parseExpectItems(n, t, p + 7, end, line, r, ex, f);
            }
            if (le == t.size()) {
                break;
            }
            ls = le + 1;
            line++;
        }
    }

    bool loadExamples(const Node *n, Rule &r) {
        if (n->kind != NK::Seq) {
            diag(code::FieldInvalid, n->mark, "'examples' must be a list, not " + what(n));
            return false;
        }
        bool ok = true, firing = false, quiet = false;
        size_t idx = 0;
        for (const yaml::NodeUP &e : n->items) {
            idx++;
            if (e->kind != NK::Map) {
                diag(code::FieldInvalid, e->mark, "an example must be a mapping (name, code or "
                    "files), not " + what(e.get()));
                ok = false;
                continue;
            }
            size_t before = m_errors;
            checkKeys(e.get(), {"name", "code", "files"}, "in an example");
            Example x;
            x.mark = e->mark;
            x.name = "example " + std::to_string(idx);
            if (const Node *nm = e->get("name")) {
                if (nm->kind != NK::String) {
                    diag(code::FieldInvalid, nm->mark, "an example's 'name' must be a string, not "
                        + what(nm));
                } else {
                    x.name = nm->s;
                }
            }
            const Node *text = e->get("code"), *files = e->get("files");
            if (text && files) {
                diag(code::FieldInvalid, e->mark, "example '" + x.name + "' has both 'code' and "
                    "'files'; use one");
            } else if (!text && !files) {
                diag(code::FieldInvalid, e->mark, "example '" + x.name + "' has neither 'code' "
                    "nor 'files'");
            } else if (text) {
                if (text->kind != NK::String) {
                    diag(code::FieldInvalid, text->mark, "an example's 'code' must be a string, "
                        "not " + what(text));
                } else {
                    x.files.push_back({"example.sv", text->s, {}});
                    parseExpectations(text, r, x.name, x.files.back());
                }
            } else if (files->kind != NK::Map || files->members.empty()) {
                diag(code::FieldInvalid, files->mark, "an example's 'files' must be a non-empty "
                    "mapping of file names to text, not " + (files->kind == NK::Map
                    ? std::string("an empty mapping") : what(files)));
            } else {
                for (const Node::Member &m : files->members) {
                    if (m.value->kind != NK::String) {
                        diag(code::FieldInvalid, m.value->mark, "file '" + m.key + "' of example '"
                            + x.name + "' must be a string, not " + what(m.value.get()));
                        continue;
                    }
                    x.files.push_back({m.key, m.value->s, {}});
                    parseExpectations(m.value.get(), r, x.name, x.files.back());
                }
            }
            if (m_errors != before) {
                ok = false;
                continue;
            }
            bool fires = false;
            for (const ExampleFile &f : x.files) {
                fires |= !f.expect.empty();
            }
            (fires ? firing : quiet) = true;
            r.m_examples.push_back(std::move(x));
        }
        if (ok && m_builtin && !(firing && quiet)) {
            diag(code::FieldInvalid, n->mark, std::string("a built-in rule needs at least one "
                "example that fires and one that does not; this one has ")
                + (firing ? "none that does not" : "none that fires"));
            ok = false;
        }
        return ok;
    }

    // Records what a checked expression calls, for the rule's level and scope.
    void addCalls(const cel::CheckResult &res, std::unordered_set<const cel::Overload *> &calls) {
        for (const cel::Overload *o : res.overloads) {
            calls.insert(o);
        }
    }

    void loadRule(const Node *n, size_t index, RuleSet &set);

    bool loadMessage(const Node *n, Rule &r, cel::CheckEnv &env,
            std::unordered_set<const cel::Overload *> &calls);

private:
    // A file the load reads: the rule file, then each file `uses:` names.
    struct File {
        std::string                         name;
        std::string                         text;
        std::unique_ptr<yaml::LineIndex>    lines;
        yaml::NodeUP                        root;       // a used file's
    };

    const LintSource                    &m_src;
    bool                                m_builtin;
    RuleClaims                          &m_claims;
    std::vector<RuleDiagnostic>         &m_diags;
    const FileReader                    &m_reader;
    std::vector<std::unique_ptr<File>>  m_files;        // [0]: the rule file
    size_t                              m_cur = 0;      // the file being read
    std::vector<std::string>            m_useStack;     // the `uses:` chain being loaded
    std::set<std::string>               m_usedPaths;
    std::string                         m_rule;         // the rule being loaded, for diagnostics
    size_t                              m_errors = 0;

    std::vector<Source>                                     m_sources;
    std::set<std::tuple<uint32_t, uint32_t, std::string>>   m_reported;
    std::vector<cel::ExprUP>                                m_parsed;

    std::map<std::string, const Node *>     m_defaults;
    std::set<std::string>                   m_badDefaults;
    std::vector<cel::Define>                m_fileDefines;      // used files', then its own
    std::vector<const Node::Member *>       m_fileDefineKeys;
    std::vector<size_t>                     m_fileDefineFile;   // m_files
    std::vector<cel::Define>                m_usedDefines;
    std::vector<const Node::Member *>       m_usedDefineKeys;
    std::vector<size_t>                     m_usedDefineFile;
    std::vector<RuleOption>                 m_fileOptions;      // used files', then its own
    std::vector<ClassifierUP>               m_usedClassifiers;
    std::vector<size_t>                     m_fileOptionFile;
    std::vector<std::string>                m_classNames;
};

bool Loader::loadMessage(const Node *n, Rule &r, cel::CheckEnv &env,
        std::unordered_set<const cel::Overload *> &calls) {
    std::vector<Piece> pieces;
    uint32_t errOff = 0;
    std::string err;
    if (!splitTemplate(n->s, pieces, errOff, err)) {
        diag(code::ExprSyntax, markIn(n, errOff), err);
        return false;
    }
    env.vars.emplace_back("finding", r.m_findingType);
    bool ok = true;
    for (Piece &p : pieces) {
        MessagePart part;
        if (!p.expr) {
            part.text = std::move(p.text);
            r.m_messageParts.push_back(std::move(part));
            continue;
        }
        uint32_t src;
        cel::ExprUP e = parseCel(n, p.offset, p.text, src);
        if (!e) {
            ok = false;
            continue;
        }
        cel::CelChecker c(env);
        cel::CheckResult res = c.check(e.get(), src);
        checkErrors(res.errors);
        if (!res.expr) {
            ok = false;
            continue;
        }
        using TK = cel::Type::Kind;
        TK k = res.type.kind;
        if (k != TK::String && k != TK::Int && k != TK::Double && k != TK::Bool
                && k != TK::Node) {
            celDiag(code::ExprType, src, e->begin, "a message can interpolate a string, a number, "
                "a bool or a node, not '" + res.type.str() + "'");
            ok = false;
            continue;
        }
        addCalls(res, calls);
        part.text = p.text;
        part.expr = std::move(res.expr);
        part.type = res.type;
        r.m_messageParts.push_back(std::move(part));
    }
    env.vars.pop_back();
    return ok;
}

void Loader::loadRule(const Node *n, size_t index, RuleSet &set) {
    m_rule.clear();
    if (n->kind != NK::Map) {
        diag(code::FieldInvalid, n->mark, "rule " + std::to_string(index + 1) + " must be a "
            "mapping of rule fields, not " + what(n));
        return;
    }
    if (const Node *id = n->get("id")) {
        if (id->kind == NK::String) {
            m_rule = id->s;
        }
    }
    std::string label = m_rule.empty() ? "rule " + std::to_string(index + 1)
        : "rule '" + m_rule + "'";
    size_t errs0 = m_errors;
    bool failed = false;

    RuleUP r(new Rule());
    r->m_file = m_src.name;
    r->m_mark = n->mark;

    std::vector<std::string> allowed;
    for (const FieldSpec &f : ruleFields()) {
        allowed.push_back(f.name);
    }
    std::map<std::string, const Node *> own;
    for (const Node::Member &m : n->members) {
        if (isXKey(m.key)) {
            continue;
        }
        const FieldSpec *f = findField(m.key);
        if (!f) {
            diag(code::UnknownKey, m.keyMark, "unknown key '" + m.key + "' in " + label
                + didYouMean(m.key, allowed));
        } else if (f->type == FieldType::Reserved) {
            diag(code::FieldInvalid, m.keyMark, "'" + m.key + "' is reserved for rule fixes, "
                "which sv-rules version 1 does not have");
        } else {
            own[m.key] = m.value.get();
        }
    }

    // Presence, defaults, and the scalar fields. Fields are in format order,
    // so `tier` is known when `since` asks whether the rule is in the nursery.
    std::map<std::string, const Node *> val;
    for (const FieldSpec &f : ruleFields()) {
        if (f.type == FieldType::Reserved) {
            continue;
        }
        const Node *v = nullptr;
        auto it = own.find(f.name);
        if (it != own.end()) {
            v = it->second;
        } else if (f.defaultable) {
            auto d = m_defaults.find(f.name);
            if (d != m_defaults.end()) {
                v = d->second;
            } else if (m_badDefaults.count(f.name)) {
                failed = true;      // reported at the default
                continue;
            }
        }
        if (!v) {
            if (f.need == Need::Required) {
                diag(code::FieldInvalid, n->mark, label + " has no '" + f.name + "'");
            } else if (f.need == Need::Builtin && m_builtin) {
                diag(code::FieldInvalid, n->mark, label + " has no '" + f.name + "', which a "
                    "built-in rule must have");
            } else if (f.need == Need::Nursery && r->m_tier == "nursery") {
                diag(code::FieldInvalid, n->mark, label + " is in the nursery tier and has no '"
                    + f.name + "' (the rules version it entered in, \"2026.1\")");
            }
            continue;
        }
        val[f.name] = v;
        if (validScalar(f, v)) {
            setScalar(*r, f.name, v);
        }
    }

    failed |= !claim(*r, val);

    // options and match, which the expressions' environment needs.
    if (val.count("options")) {
        failed |= !loadOptions(val["options"], r->m_options);
    }
    // The file's options, after the rule's own: the rule reads them the same way.
    for (const RuleOption &o : m_fileOptions) {
        bool clash = false;
        for (const RuleOption &own : r->m_options) {
            clash |= (own.name == o.name);
        }
        if (clash) {
            const Node *opts = val["options"];
            yaml::Mark at = opts->mark;
            for (const Node::Member &m : opts->members) {
                if (m.key == o.name) {
                    at = m.keyMark;
                }
            }
            diag(code::FieldInvalid, at, "option '" + o.name + "' is also a file option (of "
                + m_files[m_fileOptionFile[&o - &m_fileOptions[0]]]->name + "); rename the "
                "rule's");
            failed = true;
        } else {
            r->m_options.push_back(o);
        }
    }
    cel::Type nodeT = cel::Type::node(0);
    if (val.count("match") && loadMatch(val["match"], *r)) {
        uint16_t base = r->m_match.classes[0];
        for (uint16_t k : r->m_match.classes) {
            base = cel::commonBase(base, k);
        }
        nodeT = cel::Type::node(base);
    } else {
        failed = true;
    }
    r->m_nodeType = nodeT;

    cel::CheckEnv env;
    env.vars.emplace_back("node", nodeT);
    for (const RuleOption &o : r->m_options) {
        env.vars.emplace_back("options." + o.name, o.celType);
    }
    env.defines = m_fileDefines;

    // define: the rule's own helpers, after the file's, which they shadow.
    if (val.count("define")) {
        std::vector<cel::Define> mine;
        std::vector<const Node::Member *> keys;
        size_t before = m_errors;
        parseDefines(val["define"], mine, &keys);
        failed |= (m_errors != before);
        for (size_t i=0; i<mine.size(); i++) {
            for (size_t j=0; j<m_fileDefines.size(); j++) {
                if (m_fileDefines[j].name == mine[i].name) {
                    const yaml::Mark &fm = m_fileDefineKeys[j]->keyMark;
                    diag(code::HelperShadowed, keys[i]->keyMark, "helper '" + mine[i].name
                        + "' shadows the file's helper of the same name ("
                        + (m_fileDefineFile[j] ? m_files[m_fileDefineFile[j]]->name + " " : "")
                        + "line " + std::to_string(fm.line) + ")",
                        RuleDiagnostic::Severity::Info);
                }
            }
            env.defines.push_back(mine[i]);
        }
    }
    {
        cel::CelChecker defs(env);
        checkErrors(defs.checkDefines());
    }

    std::unordered_set<const cel::Overload *> calls;
    if (val.count("let")) {
        const Node *l = val["let"];
        if (l->kind != NK::Map) {
            diag(code::FieldInvalid, l->mark, "'let' must be a mapping of names to expressions, "
                "in order, not " + what(l));
        } else {
            for (const Node::Member &m : l->members) {
                LetBinding b;
                b.name = m.key;
                b.type = cel::Type(cel::Type::Kind::Error);
                if (!isIdent(m.key)) {
                    diag(code::FieldInvalid, m.keyMark, "'let' name '" + m.key + "' must be an "
                        "identifier");
                } else if (std::find(Reserved.begin(), Reserved.end(), m.key) != Reserved.end()) {
                    diag(code::FieldInvalid, m.keyMark, "'let' cannot define '" + m.key + "', "
                        "which every rule already has");
                } else if (celString(m.value.get(), "let: " + m.key)) {
                    uint32_t src;
                    cel::ExprUP e = parseCel(m.value.get(), 0, m.value->s, src);
                    if (e) {
                        cel::CelChecker c(env);
                        cel::CheckResult res = c.check(e.get(), src);
                        checkErrors(res.errors);
                        b.type = res.type;
                        b.expr = std::move(res.expr);
                        addCalls(res, calls);
                    }
                }
                failed |= !b.expr;
                // A bad let is still declared, typed as an error, so the
                // expressions after it are not also reported as undeclared.
                env.vars.emplace_back(b.name, b.type);
                r->m_lets.push_back(std::move(b));
            }
        }
    }

    auto checkField = [&](const char *field, const cel::Type &expected, cel::ExprUP &out,
            cel::Type *type) {
        const Node *v = val[field];
        if (!celString(v, field)) {
            failed = true;
            return;
        }
        uint32_t src;
        cel::ExprUP e = parseCel(v, 0, v->s, src);
        if (!e) {
            failed = true;
            return;
        }
        cel::CelChecker c(env);
        cel::CheckResult res = c.check(e.get(), src, &expected, field);
        checkErrors(res.errors);
        failed |= !res.expr;
        addCalls(res, calls);
        if (type) {
            *type = res.type;
        }
        out = std::move(res.expr);
    };
    if (val.count("where")) {
        checkField("where", cel::Type(cel::Type::Kind::Bool), r->m_where, nullptr);
    }
    r->m_findingType = nodeT;
    if (val.count("report")) {
        cel::Type rt;
        checkField("report", cel::Type::list(cel::Type::node(0)), r->m_report, &rt);
        if (rt.kind == cel::Type::Kind::List && !rt.args.empty()
                && rt.args[0].kind == cel::Type::Kind::Node) {
            r->m_findingType = rt.args[0];
        }
    }
    if (val.count("message") && val["message"]->kind == NK::String) {
        failed |= !loadMessage(val["message"], *r, env, calls);
    }

    if (val.count("examples")) {
        failed |= !loadExamples(val["examples"], *r);
    }

    // Level and scope (format §3.1): the highest level, and model scope if
    // anything called is whole-model.
    for (const auto &o : cel::FunctionRegistry::get().all()) {
        if (calls.count(o.get())) {
            r->m_calls.push_back(o.get());
            if (o->level > r->m_level) {
                r->m_level = o->level;
            }
            if (o->scope == cel::Scope::Model) {
                r->m_scope = cel::Scope::Model;
            }
        }
    }

    if (!failed && m_errors == errs0) {
        set.m_rules.push_back(std::move(r));
    }
    m_rule.clear();
}

RuleSetUP Loader::load() {
    yaml::NodeUP root;
    try {
        root = yaml::parse(m_src.name, m_src.text);
    } catch (const yaml::YamlError &e) {
        diag(code::FileYaml, e.mark(), e.what());
        if (e.hasRelated) {
            RuleDiagnostic &d = m_diags.back();
            d.hasRelated = true;
            d.relatedFile = m_src.name;
            d.relatedLine = e.related.line;
            d.relatedCol = e.related.col;
        }
        return nullptr;
    }
    yaml::Mark start;
    start.line = start.col = 1;
    if (root->kind == NK::Null) {
        diag(code::FileStructure, start, "the file is empty; a rule file is a mapping with the "
            "single key 'sv-rules'");
        return nullptr;
    }
    if (root->kind != NK::Map) {
        diag(code::FileStructure, root->mark, "not a rule file: the top level is " + what(root.get())
            + ", not a mapping with the single key 'sv-rules'");
        return nullptr;
    }
    const Node *pss = root->get("sv-rules");
    if (!pss) {
        std::string hint;
        for (const Node::Member &m : root->members) {
            if (hint.empty()) {
                hint = didYouMean(m.key, {"sv-rules"});
            }
        }
        diag(code::FileStructure, root->mark, "not a rule file: there is no top-level "
            "'sv-rules' key" + hint);
        return nullptr;
    }

    // The version first: a file of a later version is refused whole, and
    // nothing else in it is judged by this version's rules.
    int32_t version = 1;
    if (pss->kind == NK::Map) {
        if (const Node *v = pss->get("version")) {
            if (v->kind != NK::Int) {
                diag(code::FileStructure, v->mark, "'version' must be an integer, not "
                    + what(v));
                return nullptr;
            }
            if (v->i != 1) {
                diag(code::FileStructure, v->mark, v->i > 1 ?
                    "this file is sv-rules version " + std::to_string(v->i) + ", but this "
                    "svcheck reads version 1; upgrade svcheck to load it" :
                    "sv-rules version " + std::to_string(v->i) + " does not exist; the "
                    "version this svcheck reads is 1");
                return nullptr;
            }
            version = 1;
        }
    }

    for (const Node::Member &m : root->members) {
        if (m.key != "sv-rules") {
            diag(code::UnknownKey, m.keyMark, "'" + m.key + "' beside 'sv-rules': "
                "'sv-rules' must be the only top-level key; file-level settings go under it");
        }
    }
    if (pss->kind != NK::Map) {
        diag(code::FileStructure, pss->mark, "'sv-rules' must be a mapping (version, uses, "
            "options, defaults, define, rules), not " + what(pss));
        return nullptr;
    }
    checkKeys(pss, {"version", "defaults", "define", "options", "uses", "contexts", "rules"},
        "under 'sv-rules'");
    const Node *rules = pss->get("rules");
    if (!rules) {
        diag(code::FileStructure, pss->mark, "'sv-rules' has no 'rules' list (write "
            "'rules: []' for a file with no rules yet)");
        return nullptr;
    }
    if (rules->kind != NK::Seq) {
        diag(code::FileStructure, rules->mark, "'rules' must be a list of rules, not "
            + what(rules));
        return nullptr;
    }

    RuleSetUP set(new RuleSet(m_src.name, m_builtin));
    set->m_version = version;
    if (const Node *d = pss->get("defaults")) {
        loadDefaults(d);
    }
    m_useStack.push_back(m_src.name);
    m_usedPaths.insert(m_src.name);
    if (const Node *u = pss->get("uses")) {
        loadUses(u);
    }
    m_useStack.clear();
    if (const Node *o = pss->get("options")) {
        std::vector<RuleOption> opts;
        loadOptions(o, opts);
        addFileOptions(o, opts);
    }
    // The used files' helpers, then this file's, which shadow them.
    m_fileDefines = m_usedDefines;
    m_fileDefineKeys = m_usedDefineKeys;
    m_fileDefineFile = m_usedDefineFile;
    if (const Node *d = pss->get("define")) {
        std::vector<cel::Define> own;
        std::vector<const Node::Member *> keys;
        parseDefines(d, own, &keys);
        for (size_t i=0; i<own.size(); i++) {
            for (size_t j=0; j<m_usedDefines.size(); j++) {
                if (m_usedDefines[j].name == own[i].name) {
                    diag(code::HelperShadowed, keys[i]->keyMark, "helper '" + own[i].name
                        + "' shadows the helper of the same name in "
                        + m_files[m_usedDefineFile[j]]->name, RuleDiagnostic::Severity::Info);
                }
            }
            m_fileDefines.push_back(own[i]);
            m_fileDefineKeys.push_back(keys[i]);
            m_fileDefineFile.push_back(0);
        }
    }
    if (!m_fileDefines.empty()) {
        cel::CheckEnv env;
        env.defines = m_fileDefines;
        for (const RuleOption &o : m_fileOptions) {
            env.vars.emplace_back("options." + o.name, o.celType);
        }
        cel::CelChecker defs(env);
        checkErrors(defs.checkDefines());
    }
    set->m_classifiers = std::move(m_usedClassifiers);
    if (const Node *c = pss->get("contexts")) {
        loadContexts(c, m_fileDefines, set->m_classifiers);
    }
    for (size_t i=0; i<rules->items.size(); i++) {
        loadRule(rules->items[i].get(), i, *set);
    }
    return set;
}

}

const std::vector<FieldSpec> &ruleFields() {
    static const std::vector<FieldSpec> fields = {
        {"id", FieldType::Code, Need::Required, false, {},
            "Stable ID, as LINT201 or ACME001"},
        {"name", FieldType::Kebab, Need::Required, false, {},
            "Unique kebab-case name, usable wherever the ID is"},
        {"tier", FieldType::Enum, Need::Required, true, catalog::taxonomy().lintTiers,
            "Lint tier, which decides the default severity and profile"},
        {"family", FieldType::Enum, Need::Builtin, true, catalog::taxonomy().families,
            "What the rule is about"},
        {"summary", FieldType::Line, Need::Required, false, {},
            "One line, not interpolated"},
        {"message", FieldType::Template, Need::Required, false, {},
            "The diagnostic text, with ${CEL} interpolation"},
        {"doc", FieldType::String, Need::Builtin, false, {},
            "Rationale, escape hatch and references (Markdown)"},
        {"lrm", FieldType::StringList, Need::Optional, false, {},
            "LRM clauses, as \"11.3.1.2\""},
        {"since", FieldType::Version, Need::Nursery, true, {},
            "The rules version the rule entered in, YYYY.N"},
        {"status", FieldType::Enum, Need::Optional, true, Statuses,
            "Lifecycle state; default active"},
        {"aliases", FieldType::CodeList, Need::Optional, false, {},
            "Former IDs"},
        {"replaced_by", FieldType::CodeList, Need::Optional, false, {},
            "IDs that replace this rule"},
        {"severity", FieldType::Enum, Need::Optional, true, Severities,
            "Override of the tier's default severity"},
        {"default_enabled", FieldType::Bool, Need::Optional, true, {},
            "Whether the rule is in its tier's profile; default true"},
        {"options", FieldType::Options, Need::Optional, false, {},
            "Declared options, read as options.<name>"},
        {"match", FieldType::Match, Need::Required, false, {},
            "The node kinds the rule runs on, and ancestor tests"},
        {"define", FieldType::Defines, Need::Optional, false, {},
            "Rule-local helpers"},
        {"let", FieldType::Lets, Need::Optional, false, {},
            "Named sub-expressions, in order"},
        {"where", FieldType::Cel, Need::Optional, false, {},
            "Filter on the matched node (bool); default true"},
        {"report", FieldType::Cel, Need::Optional, false, {},
            "The nodes to report (list of nodes); default [node]"},
        {"examples", FieldType::Examples, Need::Builtin, false, {},
            "Executable examples"},
        {"fix", FieldType::Reserved, Need::Optional, false, {},
            "Reserved for rule fixes"},
    };
    return fields;
}

const std::vector<std::string> &optionTypes() {
    static const std::vector<std::string> types = {"string", "int", "bool", "string-list"};
    return types;
}

namespace {

/**
 * True when a line of `text` starts a top-level `sv-rules` key, quoted or
 * not, in block (`sv-rules:`) or flow (`{"sv-rules": ...`) form. For text
 * that is not valid YAML, where the parse cannot answer.
 */
bool startsPssRulesKey(const std::string &text) {
    static const std::string key = "sv-rules";
    size_t i = 0;
    while (i < text.size()) {
        size_t eol = text.find('\n', i);
        if (eol == std::string::npos) {
            eol = text.size();
        }
        size_t p = i;
        while (p < eol && (text[p] == ' ' || text[p] == '\t' || text[p] == '{')) {
            p++;
        }
        char quote = 0;
        if (p < eol && (text[p] == '"' || text[p] == '\'')) {
            quote = text[p++];
        }
        if (text.compare(p, key.size(), key) == 0) {
            p += key.size();
            if (quote && p < eol && text[p] == quote) {
                p++;
            }
            while (p < eol && (text[p] == ' ' || text[p] == '\t')) {
                p++;
            }
            if (p < eol && text[p] == ':') {
                return true;
            }
        }
        i = eol + 1;
    }
    return false;
}

}

RuleProbe probeRuleText(const std::string &text) {
    yaml::NodeUP root;
    try {
        root = yaml::parse("<probe>", text);
    } catch (const yaml::YamlError &) {
        return startsPssRulesKey(text) ? RuleProbe::RuleFile : RuleProbe::NotYaml;
    }
    if (!root || root->kind == yaml::Node::Kind::Null) {
        return RuleProbe::Empty;
    }
    if (root->kind != yaml::Node::Kind::Map) {
        return RuleProbe::NotMapping;
    }
    return root->get("sv-rules") ? RuleProbe::RuleFile : RuleProbe::NoKey;
}

RuleSetUP loadRuleFile(const LintSource &src, bool builtin, RuleClaims &claims,
        std::vector<RuleDiagnostic> &diags, const FileReader &reader) {
    Loader l(src, builtin, claims, diags, reader);
    return l.load();
}

FileReader diskReader() {
    return [](const std::string &path, std::string &text) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return false;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        text = ss.str();
        return true;
    };
}

}
}
}
