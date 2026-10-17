/*
 * Classify.h
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
 * The classify stage (sv-contexts.md §5.1; svcheck-plan P1.4): the
 * `contexts:` classifiers, run over the carriers of the run's trees before
 * the rules. Experimental until the P1.6b trial settles it.
 *
 * - **Axis by axis**, in the order the classifiers first name them. After
 *   each, the labels so far go to the host (Context::applyContexts), so a
 *   later axis's classifiers can read the earlier ones' (`context(n)`).
 * - **Per carrier and axis**, the labels are resolved (§3.2): the declared
 *   tags (Context::declaredContexts) if there are any; else the strong
 *   classifiers' labels, all of them; else the weak (convention) ones'.
 *   Where a weaker source disagrees with the one used, a note says so: the
 *   data for Q-CX4.
 * - **Nested carriers sum** (Q-CX3): a carrier also has the labels of the
 *   carriers inside it, as `Nested`, under its own name.
 * - **Joining** (Q-CX5): a classifier's `joins:` carriers get its label
 *   and unit name, so the host can group carriers into one unit by name.
 *   A labelled carrier inside another joins the outer one's unit, unless
 *   the outer one is mixed (more than one label on the axis); then it
 *   keeps its own (decided at svcheck-plan P2.0).
 * - **Convention never selects** (Q-CX4): a label only convention gives is
 *   kept, for `--contexts` and the consistency rules, but does not select
 *   (ContextLabel::selects), and does not make a carrier mixed.
 *
 * The engine knows labels, axes and carriers only; the vocabulary is the
 * pack's (Q-CX2), and what a unit is, the host's.
 */
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Context.h"

namespace svcheck {
namespace lint {

namespace rules {
struct Classifier;
}

namespace run {

struct Contexts {
    std::vector<ContextLabel>   labels;     // by axis, then carrier in walk order
    std::vector<ContextNote>    notes;
};

/**
 * Runs `classifiers` over the trees `ctx` has indexed (index every root
 * first), each evaluation with a budget of `maxSteps`.
 */
Contexts classify(const std::vector<const rules::Classifier *> &classifiers, Context &ctx,
    uint64_t maxSteps=1000000);

/** "Declared", "Content", "Convention", "Nested". */
const char *sourceName(ContextLabel::Source s);

}
}
}
