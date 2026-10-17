/*
 * Cli.h
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
 * The svcheck command line, in the library (svcheck-plan P2.6b): the
 * executable's main() calls main(); IFactory::runCli and compile() parse
 * the same arguments.
 */
#pragma once
#include <optional>
#include <string>
#include <vector>
#include "Lint.h"

namespace slang {
namespace driver {
class Driver;
}
}

namespace svcheck {
namespace cli {

/**
 * Adds svcheck's options to `driver` and parses `args` (args[0] is the
 * program name) into it and `lint`; applies `--relax-lrm`. Handles
 * `--help` and `--version`. Returns -1 to go on, else the exit status.
 */
int parseArgs(slang::driver::Driver &driver, std::vector<std::string> args, LintOptions &lint,
              std::optional<bool> &dumpDiags);

/** The svcheck command: the exit status. */
int main(int argc, char **argv);

}
}
