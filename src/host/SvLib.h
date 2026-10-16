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
 * The SV host's function library (svcheck-design §3.2), library "sv". So far
 * the language-neutral group only (svcheck-plan P1.3): `isa` and navigation,
 * forked from pssparser's lib/PssLib.cpp. P1.4 adds the rest.
 */
#pragma once
#include "cel/CelFunctions.h"

namespace svcheck {
namespace host {

/** Registers the library; give it to cel::setHostLib() before the registry is first used. */
void registerSvLib(lint::cel::FunctionRegistry &r);

}
}
