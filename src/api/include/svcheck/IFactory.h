/*
 * IFactory.h
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
 * libsvcheck's entry point (svcheck-plan P2.6b; design §3.5): what the
 * Python bindings reach, through `svcheck_getFactory()`, without linking
 * the library. Pure-virtual interfaces only, so a caller needs no slang
 * header: the model is the curated one (`svcheck/sem/`, generated from
 * src/ast/*.yaml), which refers to nothing in slang.
 */
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

#if defined(_WIN32)
#  if defined(SVCHECK_BUILD_DLL)
#    define SVCHECK_API __declspec(dllexport)
#  else
#    define SVCHECK_API __declspec(dllimport)
#  endif
#else
#  define SVCHECK_API
#endif

namespace svcheck {
namespace sem {
class IDesign;
class IFactory;
}

namespace api {

/**
 * A compiled design: slang's driver and compilation, and the model lowered
 * from it. It owns all three; the model's nodes live as long as it does.
 */
class ICompilation {
public:
    virtual ~ICompilation() {}

    /** The model's root; null when the compilation has errors, or with `--semantic=none`. */
    virtual sem::IDesign *getDesign() = 0;

    /** slang's errors. */
    virtual uint32_t getErrors() = 0;

    /** slang's errors as its command line prints them; "" when there are none. */
    virtual const std::string &getDiagnostics() = 0;

    /** The path of a location's `fileid`; "" for none. */
    virtual std::string getFilePath(int32_t fileid) = 0;

    /** The user files the compilation read, absolute and sorted (not library code). */
    virtual const std::vector<std::string> &getFiles() = 0;
};

class IFactory {
public:
    virtual ~IFactory() {}

    /** svcheck's version, as `svcheck --version` prints it. */
    virtual const char *getVersion() = 0;

    /**
     * The model's factory, which the library also exports as
     * `sem_getFactory()` (pyastbuilder's entry point, which the generated
     * `svcheck.sem` module loads).
     */
    virtual sem::IFactory *getSemFactory() = 0;

    /**
     * Runs the svcheck command in-process: `args` as the executable's
     * arguments, without the program name. What it writes is returned in
     * `out` and `err`. Returns its exit status. The rule schema is
     * installed once per process: by the first run (or compile()), with
     * its `--semantic` layer.
     */
    virtual int runCli(const std::vector<std::string> &args, std::string &out,
                       std::string &err) = 0;

    /**
     * Compiles and lowers what `args` names, as the svcheck command line
     * does (`-f`, `+incdir+`, `--top`, `--relax-lrm`, `--library-dir`,
     * `--semantic`; the lint options are accepted and ignored). Returns
     * null when the arguments are wrong or name nothing, with why in `err`.
     * The caller owns the result.
     */
    virtual ICompilation *compile(const std::vector<std::string> &args, std::string &err) = 0;
};

}
}

extern "C" SVCHECK_API svcheck::api::IFactory *svcheck_getFactory();
