/*
 * Out.cpp
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
#include <cstdarg>
#include <cstdio>
#include <string>
#include "Out.h"
#include "slang/util/OS.h"

namespace svcheck {
namespace cli {

namespace {

std::string vformat(const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int n = std::vsnprintf(nullptr, 0, fmt, ap2);
    va_end(ap2);
    std::string s(n > 0 ? static_cast<size_t>(n) : 0, '\0');
    if (n > 0) {
        std::vsnprintf(s.data(), s.size() + 1, fmt, ap);
    }
    return s;
}

}

void outf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::string s = vformat(fmt, ap);
    va_end(ap);
    slang::OS::print(s);
}

void errf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::string s = vformat(fmt, ap);
    va_end(ap);
    slang::OS::printE(s);
}

}
}
