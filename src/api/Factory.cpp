/*
 * Factory.cpp
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
 * libsvcheck's factory (svcheck-plan P2.6b): the svcheck command and the
 * compilation, in-process.
 */
#include <memory>
#include <optional>
#include "svcheck/IFactory.h"
#include "svcheck/sem/FactoryExt.h"
#include "Cli.h"
#include "Lint.h"
#include "../host/Lint.h"
#include "Schema.h"
#include "SvLib.h"
#include "cel/CelFunctions.h"
#include "slang/diagnostics/TextDiagnosticClient.h"
#include "slang/driver/Driver.h"
#include "slang/util/OS.h"

namespace svcheck {
namespace api {

namespace {

class Compilation : public ICompilation {
public:
    sem::IDesign *getDesign() override { return m_lc ? m_lc->design() : nullptr; }

    uint32_t getErrors() override { return m_lc ? m_lc->errors() : 0; }

    const std::string &getDiagnostics() override { return m_diags; }

    std::string getFilePath(int32_t fileid) override {
        if (!m_lc || fileid < 0) {
            return "";
        }
        const std::vector<std::string> &paths = m_lc->paths();
        return static_cast<size_t>(fileid) < paths.size() ? paths[fileid] : "";
    }

    const std::vector<std::string> &getFiles() override {
        static const std::vector<std::string> none;
        return m_lc ? m_lc->files() : none;
    }

    slang::driver::Driver                   m_driver;
    std::unique_ptr<host::LintCompilation>  m_lc;
    std::string                             m_diags;
};

std::vector<std::string> withProgram(const std::vector<std::string> &args) {
    std::vector<std::string> a{"svcheck"};
    a.insert(a.end(), args.begin(), args.end());
    return a;
}

class Factory : public IFactory {
public:
    const char *getVersion() override { return SVCHECK_VERSION; }

    sem::IFactory *getSemFactory() override { return sem_getFactory(); }

    int runCli(const std::vector<std::string> &args, std::string &out,
               std::string &err) override {
        std::vector<std::string> a = withProgram(args);
        std::vector<char *> argv;
        for (std::string &s : a) {
            argv.push_back(s.data());
        }
        argv.push_back(nullptr);
        int rc;
        {
            auto guard = slang::OS::captureOutput();
            rc = cli::main(static_cast<int>(a.size()), argv.data());
        }
        out = std::move(slang::OS::capturedStdout);
        err = std::move(slang::OS::capturedStderr);
        slang::OS::capturedStdout.clear();
        slang::OS::capturedStderr.clear();
        return rc;
    }

    ICompilation *compile(const std::vector<std::string> &args, std::string &err) override {
        std::unique_ptr<Compilation> c(new Compilation());
        cli::LintOptions lint;
        std::optional<bool> dumpDiags;
        bool ok;
        {
            auto guard = slang::OS::captureOutput();
            int rc = cli::parseArgs(c->m_driver, withProgram(args), lint, dumpDiags);
            ok = rc < 0 && c->m_driver.processOptions();
            if (ok) {
                host::installSchema(lint.layer);
                lint::cel::setHostLib(&host::registerSvLib);
                (void)c->m_driver.parseAllSources();
                c->m_lc.reset(new host::LintCompilation(c->m_driver, lint.layer,
                    lint.libraryDirs));
                c->m_diags = c->m_driver.textDiagClient->getString();
            }
        }
        err = slang::OS::capturedStderr + slang::OS::capturedStdout;
        slang::OS::capturedStdout.clear();
        slang::OS::capturedStderr.clear();
        if (!ok) {
            if (err.empty()) {
                err = "svcheck: nothing to compile\n";
            }
            return nullptr;
        }
        return c.release();
    }
};

}

}
}

extern "C" SVCHECK_API svcheck::api::IFactory *svcheck_getFactory() {
    static svcheck::api::Factory factory;
    return &factory;
}
