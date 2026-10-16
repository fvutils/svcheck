// svcheck: native entry point.
//
// P0 (svcheck-plan.md P0.4): compile the sources named on the command line
// (slang's standard arguments, so -f, +incdir+ and +define+ work) and, with
// --dump-diags, report slang's diagnostic counts and the time taken.
//
// P1.1: `svcheck --engine-test-hooks` serves the rule engine's test hooks
// (LintTestHooks::dispatch), one JSON request per stdin line and one JSON
// reply per stdout line, for tests/engine. Not a user option.

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <optional>
#include <string>

#include "HookParser.h"
#include "LintTestHooks.h"
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

static int serveTestHooks() {
    // Before any hook: the engine indexes the schema on first use.
    svcheck::host::installSyntaxSchema();
    svcheck::lint::LintTestHooks hooks;
    hooks.setParser(svcheck::host::syntaxHookParser());
    std::string line;
    while (std::getline(std::cin, line)) {
        std::cout << hooks.dispatch(line) << '\n' << std::flush;
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--engine-test-hooks") == 0)
        return serveTestHooks();

    driver::Driver driver;
    driver.addStandardArgs();

    std::optional<bool> showHelp;
    std::optional<bool> showVersion;
    std::optional<bool> dumpDiags;
    driver.cmdLine.add("-h,--help", showHelp, "Display available options");
    driver.cmdLine.add("--version", showVersion, "Display version information and exit");
    driver.cmdLine.add("--dump-diags", dumpDiags,
                       "Compile, then print slang's diagnostic counts and timing");

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
