/*
 * Out.h
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
 * Where the CLI writes (svcheck-plan P2.6b): through slang's OS::print and
 * OS::printE, never stdio directly, so that a run inside the library
 * (`IFactory::runCli`, the Python bindings) is captured with
 * slang::OS::captureOutput(), slang's own messages with it.
 */
#pragma once

namespace svcheck {
namespace cli {

/** printf to stdout. */
void outf(const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

/** printf to stderr. */
void errf(const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

}
}
