// svcheck: native entry point.
//
// P0 (svcheck-plan.md P0.4): compile the sources named on the command line
// (slang's standard arguments, so -f, +incdir+ and +define+ work) and, with
// --dump-diags, report slang's diagnostic counts and the time taken.
//
// P1.1: `svcheck --engine-test-hooks` serves the rule engine's test hooks
// (LintTestHooks::dispatch), one JSON request per stdin line and one JSON
// reply per stdout line, for tests/engine. Not a user option.
//
// P1.5: `--rules FILE` lints the compilation with the rules of FILE
// (Lint.h); `--contexts` prints its units instead.

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <string>

#include "HookParser.h"
#include "Lint.h"
#include "LintTestHooks.h"
#include "Schema.h"
#include "SvLib.h"
#include "Syntax.h"

#include "slang/ast/Compilation.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/driver/Driver.h"
#include "slang/util/VersionInfo.h"

using namespace slang;
using Clock = std::chrono::steady_clock;

static double seconds(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}

// --engine-test-hooks [--semantic=none|model|slang] [--uvm=DIR]: with a
// semantic layer (P1.3), the node hooks compile their source, against
// uvm_pkg from DIR (UVM's src directory) when it mentions `uvm_`.
static int serveTestHooks(int argc, char** argv) {
    using svcheck::host::SemanticLayer;
    SemanticLayer layer = SemanticLayer::None;
    std::string uvmDir;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a.rfind("--semantic=", 0) == 0 &&
            svcheck::host::semanticLayerOf(a.substr(11), layer))
            continue;
        if (a.rfind("--uvm=", 0) == 0) {
            // Absolute: library code is found by comparing absolute paths.
            uvmDir = std::filesystem::absolute(a.substr(6)).lexically_normal().string();
            continue;
        }
        std::fprintf(stderr, "svcheck --engine-test-hooks: unknown option %s\n", a.c_str());
        return 2;
    }
    // Before any hook: the engine indexes the schema, and builds the
    // function registry, on first use.
    svcheck::host::installSchema(layer);
    svcheck::lint::cel::setHostLib(&svcheck::host::registerSvLib);
    svcheck::lint::LintTestHooks hooks;
    // The snippet hooks take a source with syntax errors; examples may not.
    hooks.setParser(svcheck::host::hookParser(svcheck::host::hostCompiler(layer, uvmDir, false)));
    hooks.setCompiler(svcheck::host::hostCompiler(layer, uvmDir, true));
    std::string line;
    while (std::getline(std::cin, line)) {
        std::cout << hooks.dispatch(line) << '\n' << std::flush;
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "--engine-test-hooks") == 0)
        return serveTestHooks(argc, argv);

    driver::Driver driver;
    driver.addStandardArgs();

    std::optional<bool> showHelp;
    std::optional<bool> showVersion;
    std::optional<bool> dumpDiags;
    driver.cmdLine.add("-h,--help", showHelp, "Display available options");
    driver.cmdLine.add("--version", showVersion, "Display version information and exit");
    driver.cmdLine.add("--dump-diags", dumpDiags,
                       "Compile, then print slang's diagnostic counts and timing");

    svcheck::cli::LintOptions lint;
    std::optional<std::string> format, semantic;
    std::optional<bool> contexts;
    driver.cmdLine.add("--rules", lint.rules,
                       "Check the sources with the rules of this sv-rules file (repeatable)",
                       "<file>");
    driver.cmdLine.add("--format", format, "How findings are written: text (default), json "
                       "or sarif", "<format>");
    driver.cmdLine.add("--semantic", semantic, "The semantic layer rules see: model (default), "
                       "slang or none", "<layer>");
    driver.cmdLine.add("--library-dir", lint.libraryDirs, "Code under this directory is "
                       "library code: not checked, and in_library() (uvm_pkg's always is)",
                       "<dir>");
    std::optional<bool> stats;
    driver.cmdLine.add("--stats", stats, "Report each phase's time and the heap in use after "
                       "it: on stderr, or as 'stats' in the JSON output");
    driver.cmdLine.add("--contexts", contexts, "Print each unit the rules' classifiers find, "
                       "with its labels and their evidence, instead of findings");

    if (!driver.parseCommandLine(argc, argv))
        return 1;

    if (showHelp == true) {
        std::printf("%s\n", driver.cmdLine.getHelpText("svcheck: SystemVerilog checker").c_str());
        return 0;
    }
    if (showVersion == true) {
        // No slang git hash: slang is built from a release archive with no
        // .git, and its version script then picks up svcheck's own hash.
        std::printf("svcheck %s (slang %d.%d.%d)\n", SVCHECK_VERSION, VersionInfo::getMajor(),
                    VersionInfo::getMinor(), VersionInfo::getPatch());
        return 0;
    }

    if (format) {
        if (!svcheck::cli::isFormat(*format)) {
            std::fprintf(stderr, "svcheck: unknown --format '%s' (text, json, sarif)\n",
                         format->c_str());
            return 2;
        }
        lint.format = *format;
    }
    if (semantic && !svcheck::host::semanticLayerOf(*semantic, lint.layer)) {
        std::fprintf(stderr, "svcheck: unknown --semantic '%s' (model, slang, none)\n",
                     semantic->c_str());
        return 2;
    }
    lint.contexts = contexts == true;
    lint.stats = stats == true;
    if (!lint.rules.empty() || lint.contexts) {
        if (lint.rules.empty()) {
            std::fprintf(stderr, "svcheck: --contexts needs --rules: the classifiers are in "
                         "the rule files\n");
            return 2;
        }
        return svcheck::cli::runLint(driver, lint);
    }

    if (!driver.processOptions())
        return 1;

    auto t0 = Clock::now();
    bool parsed = driver.parseAllSources();
    auto t1 = Clock::now();
    auto compilation = driver.createCompilation();
    auto& diags = compilation->getAllDiagnostics();
    auto t2 = Clock::now();

    size_t errors = 0, warnings = 0;
    std::map<std::string, size_t> byCode;
    for (auto& d : diags) {
        byCode[std::string(toString(d.code))]++;
        if (d.isError())
            errors++;
        else
            warnings++;
    }

    if (dumpDiags == true) {
        std::printf("files:       %zu\n", driver.syntaxTrees.size());
        std::printf("diagnostics: %zu (%zu errors, %zu warnings)\n", diags.size(), errors,
                    warnings);
        std::printf("parse:       %.3f s\n", seconds(t0, t1));
        std::printf("elaborate:   %.3f s\n", seconds(t1, t2));
        std::printf("total:       %.3f s\n", seconds(t0, t2));
        for (auto& [code, n] : byCode)
            std::printf("  %-28s %zu\n", code.c_str(), n);
        return (parsed && errors == 0) ? 0 : 1;
    }

    driver.reportCompilation(*compilation, /*quiet*/ true);
    return driver.reportDiagnostics(/*quiet*/ true) ? 0 : 1;
}
