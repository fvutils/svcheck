/*
 * Location.h
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
 * A source position, as pssparser's ast::Location (pyastbuilder's schema
 * struct): the host's file id, 1-based line and column, and the extent in
 * characters. -1: unset.
 */
#pragma once
#include <stdint.h>

namespace svcheck {
namespace ast {

struct Location {
    int32_t fileid = -1;
    int32_t lineno = -1;
    int32_t linepos = -1;
    int32_t extent = -1;
};

} // namespace ast
} // namespace svcheck
