/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef SPELLLSP_SERVER_H
#define SPELLLSP_SERVER_H

#include "parsers.h"
#include <filesystem>
#include <format>
#include <fstream>
#include <hunspell/hunspell.hxx>
#include <lsp/connection.h>
#include <lsp/fileuri.h>
#include <lsp/io/standardio.h>
#include <lsp/messagehandler.h>
#include <lsp/types.h>
#include <map>

struct correction {
    std::string text;
    lsp::Diagnostic diagnostic;
    std::vector<std::string> suggestions;
};

class spell_lsp_server {
    lsp::Connection connection{ lsp::io::standardIO() };
    std::map<lsp::DocumentUri, lsp::TextDocumentItem> documents;
    Hunspell& hunspell;
    std::string root_dir;
    std::filesystem::path local_dic;
    std::vector<std::string> runtime_words;
    parser<parser_kind::LATEX> parser;

public:
    lsp::MessageHandler msg_handler;
    bool is_running;

    spell_lsp_server(Hunspell& _hunspell);

    inline std::vector<correction> diagnose(const std::string_view text) {
        std::vector<correction> corrections;
        auto lines = text | std::views::split('\n') | std::views::enumerate;

        for (const auto& [line_nr, line_range] : lines) {
            const std::string_view line{ line_range };
            try {
                for (const auto& submatch : parser.parse(line)) {
                    std::string word{ submatch.match };
                    auto col = submatch.match_offset;
                    if (!hunspell.spell(word)) {
                        auto suggestions = hunspell.suggest(word);
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
            } catch (const std::exception& e) {
                throw std::runtime_error(std::format("line {}: {}", line_nr, e.what()));
            }
        }
        return corrections;
    }

    inline int add_local_spelling(const std::string& word) {
        runtime_words.push_back(word);
        return hunspell.add(word);
    }

    inline void save_local_words() {
        if (!runtime_words.empty()) {
            std::ofstream of{ local_dic, std::ios::app };
            for (const auto& word : runtime_words) {
                of << word << "\n";
            }
            runtime_words.clear();
        }
    }

    inline void load_local_words() {
        if (std::filesystem::exists(local_dic)) {
            std::ifstream in{ local_dic, std::ios::in };
            std::string entry;
            while (std::getline(in, entry)) {
                hunspell.add(entry);
            }
        }
    }
};

#endif

