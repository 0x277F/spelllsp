/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef SPELLLSP_PARSERS_H
#define SPELLLSP_PARSERS_H

#include <ctre-unicode.hpp>

namespace parsers {
// matches a string of LaTeX source and tries to capture words that need spellchecking in group 1
static constexpr auto latex = ctre::search_all<
    R"((?:\\text\w\w\{?)|(?:\\[\p{L}_@]+(?:[\{\[].*[\}\]])*)|`*((?:\p{L}(?:['\-]\p{L})?)+)'*)">;

} // namespace parsers

#endif

