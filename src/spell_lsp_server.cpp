/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "lsp/fileuri.h"
#include "spell_lsp_server.h"
#include <algorithm>
#include <filesystem>
#include <format>
#include <future>
#include <hunspell/hunspell.hxx>
#include <lsp/messages.h>
#include <lsp/types.h>
#include <print>
#include <ranges>
#include <variant>

spell_lsp_server::spell_lsp_server(const std::string& dic_file, const std::string& aff_file)
    : msg_handler{ connection }, is_running{ true },
      hunspell{ std::make_unique<Hunspell>(dic_file.c_str(), aff_file.c_str()) } {

    msg_handler
        .add<lsp::requests::Initialize>([&](auto&& params) {
            auto root_path = params.rootPath.value_or(lsp::Nullable<std::string>{});
            this->root_dir = root_path.isNull() ? "." : *root_path;
            this->local_dic = std::filesystem::absolute(std::filesystem::path{ root_dir } / ".spelling.dic");
            load_local_words();
            lsp::requests::Initialize::Result result{
                .capabilities = { .textDocumentSync = lsp::TextDocumentSyncKind::Full,
                                  .codeActionProvider = true,
                                  .executeCommandProvider = lsp::ExecuteCommandOptions{
                                      .commands = std::vector{ std::string{ "spell_addlocal" } }
                                  },
                                  .diagnosticProvider = lsp::
                                      DiagnosticOptions{ .interFileDependencies = false,
                                                         .workspaceDiagnostics = false,
                                                         .identifier = "spell" },

                                },
                .serverInfo = lsp::InitializeResultServerInfo{ .name = "spell" },
            };

            return result;
        })
        .add<lsp::notifications::TextDocument_DidOpen>([&](auto&& params) {
            this->documents[params.textDocument.uri] = params.textDocument;
        })
        .add<lsp::notifications::TextDocument_DidClose>([&](auto&& params) {
            this->documents.erase(params.textDocument.uri);
        })
        .add<lsp::notifications::TextDocument_DidChange>([&](auto&& params) {
            for (auto& change : params.contentChanges) {
                if (std::holds_alternative<lsp::TextDocumentContentChangeEvent_Text>(change)) {
                    documents[params.textDocument.uri]
                        .text = std::get<lsp::TextDocumentContentChangeEvent_Text>(change).text;
                }
            }
        })
        .add<lsp::requests::Shutdown>([&]() {
            save_local_words();
            return lsp::requests::Shutdown::Result{};
        })
        .add<lsp::notifications::Exit>([&]() {
            is_running = false;
        })
        .add<lsp::requests::TextDocument_Diagnostic>([&](auto&& params) {
            return std::async(
                std::launch::deferred,
                [&](auto document) {
                    lsp::requests::TextDocument_Diagnostic::Result result{};
                    auto corrections = diagnose(document.text);
                    lsp::RelatedFullDocumentDiagnosticReport report{};
                    report.items = std::vector{ std::from_range,
                                                corrections | std::views::transform([](const auto& corr) {
                                                    return corr.diagnostic;
                                                }) };
                    result = report;
                    return result;
                },
                documents[params.textDocument.uri]);
        })
        .add<lsp::requests::TextDocument_CodeAction>([&](auto&& params) {
            return std::async(
                std::launch::deferred,
                [&](auto&& params, auto document) {
                    auto cursor = params.range.start;
                    auto all_corrections = diagnose(document.text);
                    auto corrections = all_corrections | std::views::filter([&](const auto& corr) {
                                           const auto range = corr.diagnostic.range;
                                           return cursor.line == range.start.line
                                                  && cursor.character
                                                         == std::clamp(cursor.character,
                                                                       range.start.character,
                                                                       range.end.character);
                                       });
                    auto actions = corrections | std::views::transform([](const auto& corr) {
                                       return std::views::zip(std::ranges::repeat_view(corr),
                                                              corr.suggestions);
                                   })
                                   | std::views::join | std::views::take(4)
                                   | std::views::transform([&](const auto& pair) {
                                         const auto& [corr, suggestion] = pair;
                                         lsp::Map<lsp::DocumentUri, std::vector<lsp::TextEdit>> edits;
                                         edits[params.textDocument.uri] = std::vector{ lsp::TextEdit{
                                             corr.diagnostic.range, suggestion } };
                                         return lsp::CodeAction{
                                             .title = corr.text + " -> " + suggestion,
                                             .kind = lsp::CodeActionKind::QuickFix,
                                             .diagnostics = std::vector{ corr.diagnostic },
                                             .edit = lsp::WorkspaceEdit{ .changes = std::move(edits) }
                                         };
                                     });

                    lsp::requests::TextDocument_CodeAction::Result result = std::vector{
                        std::from_range, actions | std::views::transform([](auto action) {
                                             return std::variant<lsp::Command, lsp::CodeAction>{ action };
                                         })
                    };
                    if (!result->empty()) {
                        const auto& first_correction = *std::ranges::begin(corrections);
                        auto& first_action = std::get<lsp::CodeAction>(result->at(0));
                        first_action.isPreferred = true;
                        result->emplace_back(lsp::CodeAction{
                            .title = "add \"" + first_correction.text + "\" to " + this->local_dic.native(),
                            .kind = lsp::CodeActionKind::QuickFix,
                            .command = lsp::Command{ .title = "add to local dictionary",
                                                     .command = "spell_addlocal",
                                                     .arguments = lsp::LSPArray{ first_correction.text } } });
                    }
                    return result;
                },
                std::forward<lsp::CodeActionParams>(params),
                this->documents[params.textDocument.uri]);
        })
        .add<lsp::requests::Workspace_ExecuteCommand>([&](auto&& params) {
            lsp::requests::Workspace_ExecuteCommand::Result result;
            if (params.command == "spell_addlocal") {
                if (params.arguments->size() == 1) {
                    add_local_spelling(params.arguments->at(0).string());
                }
            }
            return result;
        });
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::println(stderr, "usage: {} <language>", argv[0]);
        return -1;
    }

    auto affpath = std::format("/usr/share/hunspell/{}.aff", argv[1]);
    auto dpath = std::format("/usr/share/hunspell/{}.dic", argv[1]);

    spell_lsp_server server{ affpath, dpath };

    while (server.is_running) {
        server.msg_handler.processIncomingMessages();
    }
    server.save_local_words();
}

