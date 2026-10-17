/*
 * SlangWarnings.h
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
 *
 * slang's warnings as svcheck markers (svcheck-plan P2.3; Q-UVM4, decided
 * by the user 2026-10-17): each warning option is a marker `SLANG###`,
 * named as the option (`unused-variable`), numbered by the append-only
 * table `slang_warnings.txt` (scripts/gen_slang_warnings.py). Its tier is
 * `suspicious` when slang's `default` group has it, else `pedantic`.
 *
 * slang's own `-W` options still decide what slang reports; svcheck's
 * selection decides what is shown, and asks slang to report what it
 * selects (warningOptions()).
 */
#pragma once
#include <set>
#include <string>
#include <vector>

namespace svcheck {
namespace host {

struct SlangWarningInfo {
    int             num;            // 0 ends the table
    const char      *name;          // the option name
    bool            inDefault;      // in slang's `default` group
    bool            removed;        // slang no longer has it: retired
    const char      *summary;       // slang's message, its arguments as "…"
};

/** Every SLANG marker, in number order; the last entry's name is null (generated). */
const SlangWarningInfo *slangWarnings();

/** "SLANG042". */
std::string slangMarkerId(int num);

/** The marker of a slang warning option; null for none. */
const SlangWarningInfo *slangWarningByName(const std::string &option);

/** The SLANG markers, for ILintEngine::setHostMarkers(): {"markers": [...]}. */
std::string slangHostMarkersJson();

/**
 * slang's `-W` options that make it report the SLANG markers in `ids` (those
 * some selection group enables): `-W<name>` for each one not in slang's
 * default group.
 */
std::vector<std::string> slangWarningOptions(const std::set<std::string> &ids);

}
}
