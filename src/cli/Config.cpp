/*
 * Config.cpp
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
#include <filesystem>
#include <fstream>
#include <sstream>
#include "Config.h"
#include "Out.h"
#include "rules/BuiltinRules.h"
#include "util/JsonWriter.h"

namespace svcheck {
namespace cli {

namespace {

namespace fs = std::filesystem;
using NK = lint::yaml::Node::Kind;

bool strings(const lint::yaml::Node *n, const std::string &key, std::vector<std::string> &out,
        std::string &err) {
    if (!n || n->kind == NK::Null) {
        return true;
    }
    if (n->kind == NK::String) {
        out.push_back(n->s);
        return true;
    }
    if (n->kind != NK::Seq) {
        err += key + ": not a string or a list of strings\n";
        return false;
    }
    for (const lint::yaml::NodeUP &i : n->items) {
        if (i->kind != NK::String) {
            err += key + ": not a string or a list of strings\n";
            return false;
        }
        out.push_back(i->s);
    }
    return true;
}

}

void writeJson(lint::JsonWriter &w, const lint::yaml::Node *n) {
    if (!n) {
        w.valueNull();
        return;
    }
    switch (n->kind) {
    case NK::Null: w.valueNull(); break;
    case NK::Bool: w.value(n->b); break;
    case NK::Int: w.value(n->i); break;
    case NK::Float: w.value(n->d); break;
    case NK::String: w.value(n->s); break;
    case NK::Seq:
        w.beginArray();
        for (const lint::yaml::NodeUP &i : n->items) {
            writeJson(w, i.get());
        }
        w.endArray();
        break;
    case NK::Map:
        w.beginObject();
        for (const lint::yaml::Node::Member &m : n->members) {
            w.key(m.key);
            writeJson(w, m.value.get());
        }
        w.endObject();
        break;
    }
}

bool readConfig(const std::string &path, Config &out, std::string &err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = path + ": cannot read the configuration\n";
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out.path = path;
    std::error_code ec;
    out.dir = fs::absolute(fs::path(path), ec).parent_path().lexically_normal().string();
    try {
        out.doc = lint::yaml::parse(path, ss.str());
    } catch (const lint::yaml::YamlError &e) {
        err = std::string(e.what()) + "\n";
        return false;
    }
    const lint::yaml::Node *root = out.doc ? out.doc->get("svcheck") : nullptr;
    if (!out.doc || out.doc->kind != NK::Map || !root || root->kind != NK::Map) {
        err = path + ": a configuration is a mapping under `svcheck:`\n";
        return false;
    }
    out.root = root;
    static const std::vector<std::string> keys = {"rules", "profile", "select", "ignore",
        "rules-version", "rule", "severity", "warnings", "overrides", "contexts", "preset"};
    for (const lint::yaml::Node::Member &m : root->members) {
        if (std::find(keys.begin(), keys.end(), m.key) == keys.end()) {
            err += path + ": svcheck." + m.key + ": unknown key\n";
        }
    }
    if (const lint::yaml::Node *p = root->get("preset")) {
        if (p->kind == NK::String && !p->s.empty()) {
            out.preset = p->s;
        } else if (p->kind != NK::Null) {
            err += path + ": svcheck.preset: not a preset name\n";
        }
    }
    std::vector<std::string> rules;
    strings(root->get("rules"), path + ": svcheck.rules", rules, err);
    for (const std::string &r : rules) {
        fs::path p(r);
        out.rules.push_back((p.is_absolute() ? p : fs::path(out.dir) / p).lexically_normal()
            .string());
    }
    if (const lint::yaml::Node *cs = root->get("contexts")) {
        if (cs->kind != NK::Seq) {
            err += path + ": svcheck.contexts: not a list\n";
        } else {
            for (size_t i=0; i<cs->items.size(); i++) {
                const lint::yaml::Node *c = cs->items[i].get();
                std::string key = path + ": svcheck.contexts[" + std::to_string(i) + "]";
                if (c->kind != NK::Map) {
                    err += key + ": not a mapping\n";
                    continue;
                }
                for (const lint::yaml::Node::Member &m : c->members) {
                    if (m.key != "files" && m.key != "packages" && m.key != "context"
                            && m.key != "axis") {
                        err += key + "." + m.key + ": unknown key\n";
                    }
                }
                DeclaredContextRule d;
                strings(c->get("files"), key + ".files", d.files, err);
                strings(c->get("packages"), key + ".packages", d.packages, err);
                const lint::yaml::Node *ctx = c->get("context");
                const lint::yaml::Node *axis = c->get("axis");
                if (!ctx || ctx->kind != NK::String || ctx->s.empty()) {
                    err += key + ": `context` is a label, \"label\" or \"label(unit)\"\n";
                    continue;
                }
                size_t open = ctx->s.find('(');
                d.label = ctx->s.substr(0, open);
                if (open != std::string::npos && ctx->s.back() == ')') {
                    d.unit = ctx->s.substr(open + 1, ctx->s.size() - open - 2);
                }
                if (axis && axis->kind == NK::String) {
                    d.axis = axis->s;
                } else if (axis) {
                    err += key + ".axis: not a string\n";
                }
                if (d.files.empty() && d.packages.empty()) {
                    err += key + ": `files` or `packages` must name at least one pattern\n";
                    continue;
                }
                out.contexts.push_back(d);
            }
        }
    }
    return err.empty();
}

namespace {

const std::string PRESET_DIR = "svcheck:rules/presets/";

// The `svcheck:` mapping of built-in file `f`, or null when it is not a
// preset (a preset's classifier file is a rule file, under `sv-rules:`).
lint::yaml::NodeUP presetDoc(const lint::rules::BuiltinRuleFile *f) {
    std::string name = f->name;
    if (name.compare(0, PRESET_DIR.size(), PRESET_DIR) != 0) {
        return nullptr;
    }
    try {
        lint::yaml::NodeUP doc = lint::yaml::parse(name, std::string(f->text, f->size));
        if (doc && doc->kind == NK::Map && doc->get("svcheck")) {
            return doc;
        }
    } catch (const lint::yaml::YamlError &) {
    }
    return nullptr;
}

std::string presetName(const std::string &file) {
    std::string n = file.substr(PRESET_DIR.size());
    return n.substr(0, n.find_last_of('.'));
}

}

std::vector<std::string> presetNames() {
    std::vector<std::string> out;
    for (const lint::rules::BuiltinRuleFile *f = lint::rules::builtinRuleFiles(); f->name; f++) {
        if (presetDoc(f)) {
            out.push_back(presetName(f->name));
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool readPreset(const std::string &name, Preset &out, std::string &err) {
    for (const lint::rules::BuiltinRuleFile *f = lint::rules::builtinRuleFiles(); f->name; f++) {
        if (presetName(f->name) != name) {
            continue;
        }
        lint::yaml::NodeUP doc = presetDoc(f);
        if (!doc) {
            continue;
        }
        out.name = name;
        out.file = f->name;
        out.doc = std::move(doc);
        out.root = out.doc->get("svcheck");
        if (out.root->kind != NK::Map) {
            err = out.file + ": a preset is a mapping under `svcheck:`\n";
            return false;
        }
        static const std::vector<std::string> keys = {"summary", "select", "ignore", "rule",
            "rules"};
        for (const lint::yaml::Node::Member &m : out.root->members) {
            if (std::find(keys.begin(), keys.end(), m.key) == keys.end()) {
                err += out.file + ": svcheck." + m.key + ": unknown key\n";
            }
        }
        if (const lint::yaml::Node *s = out.root->get("summary"); s && s->kind == NK::String) {
            out.summary = s->s;
        }
        std::vector<std::string> rules;
        strings(out.root->get("rules"), out.file + ": svcheck.rules", rules, err);
        for (const std::string &r : rules) {
            out.rules.push_back(PRESET_DIR + r);
        }
        return err.empty();
    }
    err = "unknown preset '" + name + "'; the presets are:\n";
    for (const std::string &n : presetNames()) {
        Preset p;
        std::string e;
        readPreset(n, p, e);
        err += "  " + n + (p.summary.empty() ? "" : ": " + p.summary) + "\n";
    }
    return false;
}

}
}
