/*
 * Examples.cpp
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
#include "Classify.h"
#include "Context.h"
#include "Examples.h"
#include "Runner.h"
#include "../rules/Rule.h"

namespace svcheck {
namespace lint {
namespace run {

namespace {

std::string at(const Where &w, bool many) {
    const std::string &name = std::get<0>(w);
    int32_t line = std::get<1>(w), col = std::get<2>(w);
    std::string pos = col ? std::to_string(line) + ":" + std::to_string(col)
        : "line " + std::to_string(line);
    return many ? name + ":" + pos : pos;
}

}

std::string ExampleResult::describe() const {
    std::string s = file + ":" + std::to_string(line) + ":" + std::to_string(col) + ": " + rule
        + " example '" + name + "'";
    for (const std::string &p : problems) {
        s += "\n  " + p;
    }
    return s;
}

std::vector<std::string> compareExpectations(const std::string &rule,
        const std::vector<Where> &expect, const std::vector<Where> &found, bool many) {
    std::vector<Where> left(found), want(expect), missing;
    std::stable_sort(want.begin(), want.end(), [](const Where &a, const Where &b) {
        return (std::get<2>(a) != 0) > (std::get<2>(b) != 0);
    });
    for (const Where &w : want) {
        auto hit = std::find_if(left.begin(), left.end(), [&w](const Where &g) {
            return std::get<0>(g) == std::get<0>(w) && std::get<1>(g) == std::get<1>(w)
                && (std::get<2>(w) == 0 || std::get<2>(w) == std::get<2>(g));
        });
        if (hit == left.end()) {
            missing.push_back(w);
        } else {
            left.erase(hit);
        }
    }
    std::vector<std::string> out;
    for (const Where &w : missing) {
        out.push_back("expected " + rule + " at " + at(w, many) + ", but it was not reported");
    }
    for (const Where &g : left) {
        out.push_back(rule + " was reported at " + at(g, many)
            + ", which the example does not expect");
    }
    return out;
}

ExampleResult runExample(const rules::Rule &rule, const rules::Example &example,
        const Compiler &compile, const std::vector<const rules::Classifier *> &classifiers) {
    ExampleResult out;
    out.rule = rule.m_id;
    out.name = example.name;
    out.file = rule.m_file;
    out.line = static_cast<int32_t>(example.mark.line);
    out.col = static_cast<int32_t>(example.mark.col);
    bool many = example.files.size() > 1;

    std::vector<SourceFile> files;
    for (const rules::ExampleFile &f : example.files) {
        files.push_back({f.name, f.text});
    }
    Trees t;
    CompileError err;
    if (!compile(files, t, err)) {
        std::string where;
        if (err.line) {
            where = at(Where(err.file, err.line, err.col), many) + ": ";
        }
        out.problems.push_back("the example has errors, so no rule ran: " + where + err.message);
        return out;
    }

    RunOptions opts;
    opts.context = t.context.get();
    if (t.context) {
        opts.fileNames = t.context->filePaths();
    }
    std::vector<const rules::Rule *> on = {&rule};
    Runner runner(on, opts);
    for (const ast::reflect::Node &r : t.roots) {
        runner.index(r);
    }
    if (!classifiers.empty() && t.context) {
        classify(classifiers, *t.context);
    }
    for (const ast::reflect::Node &r : t.roots) {
        runner.run(r);
    }
    auto fileOf = [&runner](int32_t fileid) {
        std::string path;
        return runner.context().filePath(fileid, path) ? path : std::string();
    };
    std::vector<Where> found;
    for (const Finding &f : runner.findings()) {
        out.markers.push_back({rule.m_id, fileOf(f.loc.fileid), f.loc.lineno, f.loc.linepos,
            f.message});
        found.emplace_back(out.markers.back().file, f.loc.lineno, f.loc.linepos);
    }
    for (const RuleFailure &f : runner.failures()) {
        out.markers.push_back({f.code, fileOf(f.loc.fileid), f.loc.lineno, f.loc.linepos,
            f.message});
    }

    std::vector<Where> expect;
    for (const rules::ExampleFile &f : example.files) {
        for (const rules::Expectation &e : f.expect) {
            expect.emplace_back(f.name, static_cast<int32_t>(e.line), static_cast<int32_t>(e.col));
        }
    }
    out.problems = compareExpectations(rule.m_id, expect, found, many);
    for (const ExampleMarker &m : out.markers) {
        if (m.code != rule.m_id) {
            out.problems.push_back("the rule failed at " + at(Where(m.file, m.line, m.col), many)
                + ": " + m.message);
        }
    }
    return out;
}

}
}
}
