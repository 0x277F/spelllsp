/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef SPELLLSP_SERVER_H
#define SPELLLSP_SERVER_H

#include "parsers.h"
#include "pcre_iterator.h"
#include <filesystem>
#include <fstream>
#include <hunspell/hunspell.hxx>
#include <lsp/connection.h>
#include <lsp/fileuri.h>
#include <lsp/io/standardio.h>
#include <lsp/messagehandler.h>
#include <lsp/types.h>
#include <map>

// matches a string of LaTeX source and tries to capture words that need spellchecking in group 1
static constexpr std::string_view LATEX_TOKENIZER_PATTERN =
    R"((?:\\text\w\w\{?)|(?:\\[\p{L}_@]+(?:[\{\[].*[\}\]])*)|\`*((?:\p{L}(?:[\'\-]\p{L})?)+)\'*)";

struct correction {
    std::string text;
    lsp::Diagnostic diagnostic;
    std::vector<std::string> suggestions;
};

class spell_lsp_server {
    lsp::Connection connection{ lsp::io::standardIO() };
    std::map<lsp::DocumentUri, lsp::TextDocumentItem> documents;
    std::unique_ptr<Hunspell> hunspell;
    std::string root_dir;
    std::filesystem::path local_dic;
    std::vector<std::string> runtime_words;

public:
    lsp::MessageHandler msg_handler;
    bool is_running;

    spell_lsp_server(const std::string& dic_file, const std::string& aff_file);

    std::vector<correction> diagnose(const std::string_view text) {
        std::vector<correction> corrections;
        auto lines = text | std::views::split('\n') | std::views::enumerate;
        for (const auto& [line_nr, line_range] : lines) {
            pcre_match_results<2> results{ parsers::LATEX, std::string_view{ line_range } };
            for (const auto& submatch : results | match_group_view{ 1 }) {
                std::string word{ submatch.match };
                auto col = submatch.match_offset;
                if (!hunspell->spell(word)) {
                    auto suggestions = hunspell->suggest(word);
                    lsp::Diagnostic diag{
                        .range = { .start = { .line = static_cast<uint>(line_nr),
                                              .character = static_cast<uint>(col) },
                                   .end = { .line = static_cast<uint>(line_nr),
                                            .character = static_cast<uint>(col + word.size()) } },
                        .message = suggestions.size() > 0 ? word + " -> " + suggestions[0] : word,
                        .severity = lsp::DiagnosticSeverity::Information,
                        .source = "(sp)"
                    };
                    corrections.emplace_back(std::move(word), std::move(diag), std::move(suggestions));
                }
            }
        }

        return corrections;
    }

    int add_local_spelling(const std::string& word) {
        runtime_words.push_back(word);
        return hunspell->add(word);
    }

    void save_local_words() {
        if (!runtime_words.empty()) {
            std::ofstream of{ local_dic, std::ios::app };
            for (const auto& word : runtime_words) {
                of << word << "\n";
            }
            runtime_words.clear();
        }
    }

    void load_local_words() {
        if (std::filesystem::exists(local_dic)) {
            std::ifstream in{ local_dic, std::ios::in };
            std::string entry;
            while (std::getline(in, entry)) {
                hunspell->add(entry);
            }
        }
    }
};

#endif

