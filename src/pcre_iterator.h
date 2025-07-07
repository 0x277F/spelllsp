/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef SPELLLSP_PCRE_ITERATOR_H
#define SPELLLSP_PCRE_ITERATOR_H

#include <format>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

// std::regex currently has terrible support for unicode so here we are ig
struct pcre_match {
    std::size_t match_offset;
    std::string_view match;
};

namespace detail {
template <class... Args>
inline std::string pcre_error_msg(int pcre_err_code, std::format_string<Args...> fmt, Args&&... args) {
    std::string pcre_error(64, '\0');
    pcre2_get_error_message(
        pcre_err_code, std::bit_cast<PCRE2_UCHAR*>(pcre_error.begin()), pcre_error.capacity());
    return std::format(
        "{}: ({}) {}", std::format(fmt, std::forward<Args>(args)...), pcre_err_code, pcre_error);
}
} // namespace detail

class pcre_pattern {
    pcre2_code* re;

public:
    pcre_pattern(const pcre_pattern& other) = delete;

    pcre_pattern(pcre_pattern&& other) noexcept: re{ other.re } { other.re = nullptr; }

    pcre_pattern& operator=(pcre_pattern&& other) noexcept {
        re = other.re;
        other.re = nullptr;
        return *this;
    }

    pcre_pattern& operator=(const pcre_pattern&) = delete;

    pcre_pattern(const std::string_view pattern) {
        int pcre_error{ PCRE2_ERROR_UNAVAILABLE };
        std::size_t pcre_error_offset{ 0 };

        re = pcre2_compile(std::bit_cast<PCRE2_SPTR>(pattern.begin()),
                           pattern.size(),
                           PCRE2_MULTILINE | PCRE2_UTF,
                           &pcre_error,
                           &pcre_error_offset,
                           nullptr);

        if (!re) {
            throw std::runtime_error(
                detail::pcre_error_msg(pcre_error, "compiling {} at {}", pattern, pcre_error_offset));
        }
    }

    ~pcre_pattern() { pcre2_code_free(re); }

    constexpr operator pcre2_code* const() const { return re; }
};

template <std::size_t N> requires(N >= 1)
using pcre_match_groups = std::array<pcre_match, N>;

template <std::size_t NGroups = 1> class pcre_iterator {
public:
    using difference_type = std::ptrdiff_t;
    using value_type = pcre_match_groups<NGroups>;
    using reference = const value_type&;

private:
    pcre2_code* re;
    pcre2_match_data* match_data;
    std::span<std::size_t, 2 * NGroups> ovec;
    std::string_view text;

    std::size_t offset;
    value_type value;

    void invalidate() { offset = text.size(); }

    value_type match_next() {
        int err = 0;
        int err_offset = 0;
        value_type groups{};
        if (offset < text.size()) {
            err = pcre2_match(
                re, std::bit_cast<PCRE2_SPTR>(text.begin()), text.size(), offset, 0, match_data, nullptr);
            if (err == PCRE2_ERROR_NOMATCH) {
                invalidate();
                return groups; // groups has been zero-initialized so every match_offset should be 0 and every
                               // match should be { nullptr, 0 }
            } else if (err < 0) {
                invalidate();
                throw std::runtime_error(
                    detail::pcre_error_msg(err, "matching {} (offset = {})", text, offset));
            } else {
                std::size_t match_start = ovec[0];
                std::size_t match_end = ovec[1];
                const auto matched_text = text.substr(match_start, match_end - match_start);

                std::size_t substr_len = 0;
                for (std::size_t i = 0; i < NGroups; ++i) {
                    err = pcre2_substring_length_bynumber(match_data, i, &substr_len);
                    if (err != PCRE2_ERROR_UNSET) {
                        groups[i] = { .match_offset = ovec[2 * i],
                                      .match = text.substr(ovec[2 * i], substr_len) };
                    } /*else {
                        throw std::runtime_error(
                            detail::pcre_error_msg(err, "matching group {} inside {}", i, matched_text));
                    }*/
                }
            }
        }
        return groups;
    }

public:
    pcre_iterator(pcre2_code* _re, const std::string_view _text, PCRE2_SIZE _offset,
                  pcre2_match_data* _match_data)
        : re{ _re }, text{ _text }, offset{ _offset }, match_data{ _match_data },
          ovec{ pcre2_get_ovector_pointer(_match_data), 2 * NGroups }, value{ match_next() } { }

    reference operator*() const { return value; }

    constexpr bool operator==(std::default_sentinel_t) const { return offset >= text.size(); }

    pcre_iterator& operator++() {
        offset = ovec[1];
        value = match_next();
        return *this;
    }

    pcre_iterator operator++(int) {
        pcre_iterator it{ *this };
        ++(*this);
        return it;
    }
};

static_assert(std::input_iterator<pcre_iterator<2>>);

template <std::size_t NGroups = 1> class pcre_match_results {
    pcre2_code* re;
    std::string_view text;
    pcre2_match_data* match_data;

public:
    using iterator = pcre_iterator<NGroups>;
    using sentinel = std::default_sentinel_t;

    pcre_match_results(const pcre_match_results&) = delete;

    pcre_match_results(pcre_match_results&& other) noexcept
        : re{ other.re }, text{ other.text },
          match_data{ pcre2_match_data_create_from_pattern(re, nullptr) } {
        other.re = nullptr;
    }

    pcre_match_results& operator=(const pcre_match_results&) = delete;

    pcre_match_results& operator=(pcre_match_results&& other) noexcept {
        re = other.re;
        text = other.text;
        match_data = pcre2_match_data_create_from_pattern(re, nullptr);
        other.re = nullptr;
    }

    pcre_match_results(const pcre_pattern& _pattern, std::string_view _text)
        : re{ _pattern }, text{ _text },
          match_data{ pcre2_match_data_create_from_pattern(_pattern, nullptr) } { }

    ~pcre_match_results() { pcre2_match_data_free(match_data); }

    [[nodiscard]] inline iterator begin() { return iterator{ re, text, 0, match_data }; }

    [[nodiscard]] inline sentinel end() { return std::default_sentinel; }
};

static_assert(std::ranges::input_range<pcre_match_results<2>>);

struct match_group_view: std::ranges::range_adaptor_closure<match_group_view> {
    std::size_t group;

    match_group_view(std::size_t _group): group{ _group } { }

    template <std::size_t N> constexpr auto operator()(pcre_match_results<N>& results) const {
        return std::ranges::ref_view{ results }
               | std::views::transform([g = group](const auto& match_groups) {
                     return match_groups[g];
                 })
               | std::views::filter([](const auto& group) {
                     return !group.match.empty();
                 });
    }
};

#endif

