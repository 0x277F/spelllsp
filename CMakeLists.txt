cmake_minimum_required(VERSION 3.25)

project(spelllsp)

set(CMAKE_CXX_STANDARD 26)

add_compile_options($<$<CONFIG:DEBUG>:-fsanitize=address,undefined> $<$<CONFIG:DEBUG>:-fno-omit-frame-pointer>)
add_link_options($<$<CONFIG:DEBUG>:-fsanitize=address,undefined>)

file(GLOB_RECURSE SPELL_LSP_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE SPELL_LSP_HDRS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
add_executable(spelllsp ${SPELL_LSP_HDRS} ${SPELL_LSP_SRCS})

set(LSP_FRAMEWORK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/lsp-framework/")
add_subdirectory(${LSP_FRAMEWORK_DIR})
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/lib/compile-time-regular-expressions/")

target_link_libraries(spelllsp PUBLIC lsp hunspell ctre)

