// svcheck: the executable. The command is in the library (src/cli/Cli.cpp,
// svcheck-plan P2.6b), which the Python bindings load too.

#include "Cli.h"

int main(int argc, char** argv) {
    return svcheck::cli::main(argc, argv);
}
