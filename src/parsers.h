/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef SPELLLSP_PARSERS_H
#define SPELLLSP_PARSERS_H

#include "pcre_iterator.h"
#include <memory>
#include <ranges>

enum class parser_kind { LATEX };

namespace detail {
using namespace std::string_view_literals;

template <parser_kind Kind> class parser_impl;

// matches a string of LaTeX source and tries to capture words that need spellchecking in group 1
static constexpr auto LATEX_TOKENIZER_PATTERN =
    R"((?:\\text\w\w\{?)|(?:\\[\p{L}_@]+(?:[\{\[].*[\}\]])*)|\`*((?:\p{L}(?:[\'\-]\p{L})?)+)\'*)"sv;

template <> class parser_impl<parser_kind::LATEX> {
    pcre_pattern pattern{ LATEX_TOKENIZER_PATTERN };

public:
    inline std::ranges::range auto parse(const std::string_view text) {
        pcre_iterator<2> it{ pattern.re, text, 0, pattern.match_data };
        return std::ranges::iota_view{ it, std::default_sentinel }
               | std::views::transform([](const auto& match_groups) {
                     return (*match_groups)[1];
                 })
               | std::views::filter([](const auto& word) {
                     return !word.match.empty();
                 });
    }
};

} // namespace detail

template <parser_kind Kind> class parser {
    std::unique_ptr<detail::parser_impl<Kind>> _impl;

public:
    inline parser(): _impl{ std::make_unique<detail::parser_impl<Kind>>() } { }

    inline std::ranges::range auto parse(const std::string_view text) { return _impl->parse(text); }
};

#endif

