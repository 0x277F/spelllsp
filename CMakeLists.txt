cmake_minimum_required(VERSION 3.25)

project(spelllsp)

set(CMAKE_CXX_STANDARD 26)

file(GLOB_RECURSE SPELL_LSP_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE SPELL_LSP_HDRS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_executable(spelllsp ${SPELL_LSP_HDRS} ${SPELL_LSP_SRCS})

set(LSP_FRAMEWORK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/lsp-framework/")
add_subdirectory(${LSP_FRAMEWORK_DIR})

target_link_libraries(spelllsp PUBLIC lsp hunspell pcre2-8)

