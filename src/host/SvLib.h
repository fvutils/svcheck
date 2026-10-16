/*
 * SvLib.h
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
 * The SV host's function library (svcheck-design §3.2), library "sv":
 *
 * - every layer: `isa` and navigation, forked from pssparser's
 *   lib/PssLib.cpp (P1.3); `derives_from`; `macro_of` (P1.4);
 * - the curated model only (P1.4): classes (`member`, `members`, `owner`),
 *   symbols, types and constants (`in_library`, `resolves_to`, `type_of`,
 *   `const_int`, `const_string`) and carriers (`classes`, `calls_in`).
 *   They are registered only when the model is the layer installed, since
 *   their signatures name its classes (with `--semantic=none`, only the
 *   syntax schema is installed).
 *
 * Nothing here is UVM's: its vocabulary (roles, `is_config_db`) is written
 * in YAML, as helpers.
 */
#pragma once
#include "cel/CelFunctions.h"

namespace svcheck {
namespace host {

/** Registers the library; give it to cel::setHostLib() before the registry is first used. */
void registerSvLib(lint::cel::FunctionRegistry &r);

}
}
