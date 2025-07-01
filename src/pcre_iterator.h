/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <cstddef>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

struct pcre_match {
    std::size_t match_offset;
    std::string_view match;
};

template <std::size_t N> requires(N >= 1)
using pcre_match_groups = std::array<pcre_match, N>;

template <std::size_t NGroups> class pcre_matches;

template <std::size_t NGroups = 1> class pcre_iterator {
    friend class pcre_matches<NGroups>;

    pcre2_code* re;
    pcre2_match_data* match_data;
    PCRE2_SIZE* ovec;
    std::size_t match_start, match_end;
    int pcre_error{ 0 };
    std::size_t pcre_error_offset{ 0 };
    std::string_view text;
    PCRE2_SIZE offset;

public:
    using difference_type = std::ptrdiff_t;
    using value_type = pcre_match_groups<NGroups>;

    pcre_iterator(pcre2_code* _re, const std::string_view _text, PCRE2_SIZE _offset,
                  pcre2_match_data* _match_data)
        : re{ _re }, text{ _text }, offset{ _offset }, match_data{ _match_data },
          ovec{ pcre2_get_ovector_pointer(_match_data) } {

        if (offset < text.size()) {
            pcre_error = pcre2_match(
                re, std::bit_cast<PCRE2_SPTR>(text.begin()), text.size(), offset, 0, match_data, nullptr);
            if (pcre_error == PCRE2_ERROR_NOMATCH) {
                match_start = match_end = offset = text.size();
                // offset = text.size();
            } else if (pcre_error < 0) {
                std::string msg(64, '\0');
                pcre2_get_error_message(pcre_error, std::bit_cast<PCRE2_UCHAR*>(msg.begin()), msg.capacity());
                throw std::runtime_error{ std::format("matching \"{}\": {}:{}", text, pcre_error, msg) };
            } else {
                match_start = ovec[0];
                match_end = ovec[1];
            }
        }
    }

    inline value_type operator*() const {
        if (offset >= text.size()) {
            throw std::out_of_range{ "dereferencing past-the-end pcre_iterator" };
        }
        const auto matched_text = text.substr(match_start, match_end - match_start);
        std::size_t length;
        value_type groups;
        for (std::size_t i = 0; i < NGroups; ++i) {
            if (pcre2_substring_length_bynumber(match_data, i, &length) != PCRE2_ERROR_UNSET) {
                groups[i] = { .match_offset = ovec[2 * i], .match = text.substr(ovec[2 * i], length) };
            } else {
                groups[i] = { .match_offset = 0, .match = "" };
            }
        }
        return groups;
    }

    constexpr bool operator==(std::default_sentinel_t) const { return offset >= text.size(); }

    inline auto& operator++() {
        if (offset < text.size()) {
            return *std::construct_at(this, re, text, match_end, match_data);
            // i don't think we need to std::launder here? but idk
        }
        return *this;
    }

    inline void operator++(int) { ++*this; }
};

struct pcre_pattern {
    pcre2_code* re;
    pcre2_match_data* match_data;

    pcre_pattern(const pcre_pattern&) = default;
    pcre_pattern(pcre_pattern&&) = delete;
    pcre_pattern& operator=(const pcre_pattern&) = default;
    pcre_pattern& operator=(pcre_pattern&&) = delete;

    pcre_pattern(const std::string_view pattern) {
        int pcre_error;
        std::size_t pcre_error_offset;

        re = pcre2_compile(std::bit_cast<PCRE2_SPTR>(pattern.begin()),
                           pattern.size(),
                           PCRE2_MULTILINE | PCRE2_UTF,
                           &pcre_error,
                           &pcre_error_offset,
                           nullptr);

        if (!re) {
            std::string msg(64, '\0');
            pcre2_get_error_message(pcre_error, std::bit_cast<PCRE2_UCHAR*>(msg.begin()), msg.capacity());
            throw std::runtime_error{ "compiling: " + msg };
        }
        match_data = pcre2_match_data_create_from_pattern(re, nullptr);
    }

    ~pcre_pattern() {
        pcre2_match_data_free(match_data);
        pcre2_code_free(re);
    }
};

