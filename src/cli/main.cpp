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
#include <vector>

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

// --engine-test-hooks [--semantic=none|model] [--uvm=DIR]: with a
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

// Simulator options common in vendor .f files, which slang does not know
// (svcheck-plan P2.3, P1-24): skipped with their arguments, or renamed to
// slang's. Only names slang does not define itself; a user's --cmd-ignore
// and --cmd-rename add to these. Verilator's --timing is not here: slang's
// own --timing takes min|typ|max (--cmd-ignore --timing,0 for such a list).
static const char* const vendorIgnores[] = {
    // Verilator
    "--cc,0", "--exe,0", "--main,0", "--binary,0", "--build,0", "--no-timing,0",
    "-o,1", "-Mdir,1", "--prefix,1", "-CFLAGS,1", "-LDFLAGS,1", "--trace,0", "--trace-fst,0",
    "--trace-structs,0", "--assert,0", "--coverage,0", "-O0,0", "-O1,0", "-O2,0", "-O3,0",
    "--x-assign,1", "--x-initial,1", "--autoflush,0", "-Wno-fatal,0", "-Wall,0",
    // VCS
    "-sverilog,0", "-full64,0", "+v2k,0", "-debug_access+all,0", "-debug_access+r,0",
    "-kdb,0", "-lca,0", "-ntb_opts,1", "-Mupdate,0", "-assert,1", "-cm,1", "-cm_dir,1",
    "-xprop,0", "+error,0", "+notimingcheck,0", "+nospecify,0",
    // Questa, Xcelium
    "-sv,0", "-mfcu,0", "-64bit,0", "-uvmhome,1", "-access,1", "-messagecount,1",
};
static const char* const vendorRenames[] = {
    "--top-module,--top", "-top,--top", "-timescale,--timescale",
};

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "--engine-test-hooks") == 0)
        return serveTestHooks(argc, argv);

    driver::Driver driver;
    driver.addStandardArgs();
    for (const char* c : vendorIgnores)
        (void)driver.cmdLine.addIgnoreCommand(c);
    for (const char* c : vendorRenames)
        (void)driver.cmdLine.addRenameCommand(c);

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
    // svcheck's own -W forms (-Werror, -Werror=SEL, -Wno-error=SEL) are its
    // warning policy (svcheck-plan P2.3); every other -W is slang's.
    // -Wno-error=X also reaches slang when X is one of its options or groups.
    std::vector<std::string> args(argv, argv + argc);
    svcheck::cli::takeWarningPolicy(args, lint, [&driver](const std::string &name) {
        return !driver.diagEngine.findFromOptionName(name).empty() ||
               driver.diagEngine.findDiagGroup(name) != nullptr;
    });
    std::vector<const char*> argp;
    for (const std::string& a : args)
        argp.push_back(a.c_str());
    driver.cmdLine.add("--rules", lint.rules,
                       "Check the sources with the rules of this sv-rules file (repeatable)",
                       "<file>");
    driver.cmdLine.add("--format", format, "How findings are written: text (default), json "
                       "or sarif", "<format>");
    driver.cmdLine.add("--semantic", semantic, "The semantic layer rules see: model (default) "
                       "or none", "<layer>");
    driver.cmdLine.add("--library-dir", lint.libraryDirs, "Code under this directory is "
                       "library code: not checked, and in_library() (uvm_pkg's always is)",
                       "<dir>");
    std::optional<bool> stats;
    driver.cmdLine.add("--stats", stats, "Report each phase's time and the heap in use after "
                       "it: on stderr, or as 'stats' in the JSON output");
    driver.cmdLine.add("--contexts", contexts, "Print each unit the rules' classifiers find, "
                       "with its labels and their evidence, instead of findings");

    // Selection and policy (svcheck-plan P2.3).
    std::optional<std::string> config, profile, explain, uvm;
    std::optional<bool> noWarnings, showWaived, statistics, listMarkers, checkRules, rulesFor,
        showConfig, relaxLrm;
    driver.cmdLine.add("--config", config, "Read the selection, overrides and declared "
                       "contexts from this YAML or JSON file (an `svcheck:` mapping)", "<file>");
    driver.cmdLine.add("--profile", profile, "The rule profile: minimal (default), "
                       "recommended, library, strict, all or none. Rules of a --rules file "
                       "are in every profile but none", "<name>");
    driver.cmdLine.add("--select", lint.select, "Enable the rules a selector names: an ID, a "
                       "name, an ID prefix, a namespace, a tier, family:<f> (repeatable)",
                       "<selector>");
    driver.cmdLine.add("--ignore", lint.ignore, "Disable the rules a selector names "
                       "(repeatable; beats a --select as specific)", "<selector>");
    driver.cmdLine.add("--no-warnings", noWarnings, "Drop findings of warning severity");
    driver.cmdLine.add("--show-waived", showWaived, "Also print waived findings, marked "
                       "[waived: reason]");
    driver.cmdLine.add("--statistics", statistics, "Print how many findings each code has");
    driver.cmdLine.add("--list-markers", listMarkers, "List every rule and marker svcheck "
                       "knows, then exit");
    driver.cmdLine.add("--explain", explain, "Explain a rule or marker (ID, name or old ID), "
                       "then exit", "<rule>");
    driver.cmdLine.add("--check-rules", checkRules, "Check the rule files and run each rule's "
                       "examples, then exit");
    driver.cmdLine.add("--rules-for", rulesFor, "Print the rules each source file gets, then "
                       "exit; nothing is compiled");
    driver.cmdLine.add("--show-config", showConfig, "Print the resolved selection, then exit");
    driver.cmdLine.add("--relax-lrm", relaxLrm, "Accept what commercial simulators and "
                       "Verilator accept against the LRM: every --allow-assertctl-string-scope, "
                       "--relax-package-exports, --relax-virtual-method-types, "
                       "--allow-typedef-class-for-interface-class, "
                       "--allow-virtual-iface-hier-ref, --relax-array-element-conversions, "
                       "--relax-string-compare, --relax-forward-method-refs, "
                       "--relax-logical-handle-operands, --relax-dist-precedence and "
                       "--allow-hierarchical-const");
    driver.cmdLine.add("--uvm", uvm, "UVM's src directory, which --check-rules compiles "
                       "examples against", "<dir>");

    if (!driver.parseCommandLine(static_cast<int>(argp.size()), argp.data()))
        return 1;
    // --relax-lrm (svcheck-plan P2.3): slang's relaxation flags (patches/slang
    // SL-6..SL-11, SL-12..SL-15, and slang's own --allow-hierarchical-const),
    // each unless the command line set it.
    if (relaxLrm == true) {
        using CF = ast::CompilationFlags;
        for (CF f : {CF::AllowAssertControlStringScope, CF::RelaxPackageExports,
                     CF::RelaxVirtualMethodTypes, CF::AllowForwardClassAsInterfaceClass,
                     CF::AllowVirtualIfaceHierRef, CF::RelaxArrayElementConversions,
                     CF::AllowHierarchicalConst, CF::RelaxStringCompare,
                     CF::RelaxForwardMethodRefs, CF::RelaxLogicalHandleOperands}) {
            auto& v = driver.options.compilationFlags[f];
            if (!v.has_value())
                v = true;
        }
        if (!driver.options.relaxDistPrecedence.has_value())
            driver.options.relaxDistPrecedence = true;
    }

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
            std::fprintf(stderr, "svcheck: unknown --format '%s' (text, json, sarif, github)\n",
                         format->c_str());
            return 2;
        }
        lint.format = *format;
    }
    lint.config = config.value_or("");
    lint.profile = profile.value_or("");
    lint.explain = explain.value_or("");
    lint.uvmDir = uvm.value_or("");
    lint.noWarnings = noWarnings == true;
    lint.showWaived = showWaived == true;
    lint.statistics = statistics == true;
    lint.listMarkers = listMarkers == true;
    lint.checkRules = checkRules == true;
    lint.rulesFor = rulesFor == true;
    lint.showConfig = showConfig == true;
    if (semantic && !svcheck::host::semanticLayerOf(*semantic, lint.layer)) {
        std::fprintf(stderr, "svcheck: unknown --semantic '%s' (model, none)\n",
                     semantic->c_str());
        return 2;
    }
    lint.contexts = contexts == true;
    lint.stats = stats == true;
    bool linting = !lint.rules.empty() || lint.contexts || !lint.config.empty() ||
                   !lint.profile.empty() || !lint.select.empty() || !lint.ignore.empty() ||
                   lint.query();
    if (linting) {
        if (lint.contexts && lint.rules.empty() && lint.config.empty()) {
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
