/*
 * Examples.h
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
 * Running the `examples:` of declarative rules (format §4; svcheck-plan
 * P1.4). A port of pssparser's Python harness (python/pssparser/lint/
 * examples.py at 57c6b14) into the engine, so the CLI can run it too.
 *
 * Each example is a small source whose `// expect: ID` comments mark where
 * its rule must report. An example runs on its own: the host compiles its
 * files (Compiler), and only its rule runs, with its options at their
 * defaults. It passes when the rule reports exactly the expected findings:
 * an expectation without `@ line:col` matches a finding anywhere on its
 * line. An example that does not compile, or on which the rule fails
 * (SVCHK220, SVCHK221), fails.
 */
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <vector>
#include "Reflect.h"

namespace svcheck {
namespace lint {

namespace rules {
class Rule;
struct Classifier;
struct Example;
}

namespace run {

class Context;

/**
 * The trees the host makes of some source: the roots, the context the run
 * reads (null: a plain Context), and whatever keeps the roots alive.
 */
struct Trees {
    std::vector<ast::reflect::Node>     roots;
    std::unique_ptr<Context>            context;
    std::shared_ptr<void>               keep;
};

/** A source file of an example, by the name the example gives it. */
struct SourceFile {
    std::string     name;
    std::string     text;
};

/** Why some source did not compile: its first error. `line` 0: no position. */
struct CompileError {
    std::string     file;
    int32_t         line = 0;
    int32_t         col = 0;
    std::string     message;
};

/**
 * Compiles `files` into `out`; false, with `error`, when they have errors.
 * The context must name each file as given (Context::setFiles).
 */
using Compiler = std::function<bool(const std::vector<SourceFile> &files, Trees &out,
    CompileError &error)>;

/** A marker of an example's run: a finding (code is the rule's ID) or a failure. */
struct ExampleMarker {
    std::string     code;
    std::string     file;
    int32_t         line = 0;
    int32_t         col = 0;
    std::string     message;
};

/** One example's outcome. */
struct ExampleResult {
    std::string                 rule;
    std::string                 name;
    // Where the example is defined: the rule file and its entry's position.
    std::string                 file;
    int32_t                     line = 0;
    int32_t                     col = 0;
    // What is wrong, one sentence each; empty when the example passes.
    std::vector<std::string>    problems;
    // The run's markers; empty when the example did not compile.
    std::vector<ExampleMarker>  markers;

    bool ok() const { return problems.empty(); }

    /** "FILE:L:C: ID example 'NAME'", then each problem, indented. */
    std::string describe() const;
};

/** (file, line, col) of a finding or an expectation; col 0 is "any". */
using Where = std::tuple<std::string, int32_t, int32_t>;

/**
 * The differences between `expect` and `found`, as problem sentences. An
 * exact expectation is matched first, so an anywhere-on-the-line one cannot
 * take the finding an exact one needs. `many` names the file in each
 * position (an example with `files:`).
 */
std::vector<std::string> compareExpectations(const std::string &rule,
    const std::vector<Where> &expect, const std::vector<Where> &found, bool many);

/**
 * Runs one example of `rule`, after the `classifiers` (Classify.h): an
 * example of a rule on a context needs its file's classifiers.
 */
ExampleResult runExample(const rules::Rule &rule, const rules::Example &example,
    const Compiler &compile, const std::vector<const rules::Classifier *> &classifiers={});

}
}
}
