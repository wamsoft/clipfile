cmake_minimum_required(VERSION 3.16)
add_compile_options("$<$<AND:$<C_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:C>>:/utf-8>")
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/utf-8>")
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Zc:__cplusplus>")
project(clipfile LANGUAGES C CXX)

# clipparse: pure C++ ライブラリ (吉里吉里非依存)。external/clipparse は git submodule。
# 初回 clone 後は `git submodule update --init --recursive` で取得。
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/clipparse/CMakeLists.txt")
    message(FATAL_ERROR
        "external/clipparse is empty. Run: git submodule update --init --recursive")
endif()
# CLI は吉里吉里 build では不要 (smoke test したいときだけ ON にする)。
set(CLIPPARSE_BUILD_CLI OFF CACHE BOOL "" FORCE)
add_subdirectory(external/clipparse)

# 吉里吉里プラグイン本体。tp_stub と ncbind は隣接の tp_stub / ncbind を見る
# (TPSTUB_DIR 環境変数 / CMake 変数で上書き可)。
if(NOT DEFINED TPSTUB_DIR)
    set(TPSTUB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../tp_stub
        CACHE PATH "Path to tp_stub directory")
endif()

if(NOT EXISTS "${TPSTUB_DIR}/krkrz.cmake")
    message(FATAL_ERROR
        "tp_stub not found at ${TPSTUB_DIR}. Set TPSTUB_DIR or place tp_stub there.")
endif()
include(${TPSTUB_DIR}/krkrz.cmake)

krkrz_plugin(clipfile
    NCBIND
    SOURCES
        clipclass.cpp
        clipwriter.cpp
        main.cpp
    LIBRARIES
        clipparse
)
