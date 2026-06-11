cmake_minimum_required(VERSION 3.16)
project(AICoder CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Release: 开启 LTO 与 strip 标记，让可执行文件大小跟 xmake 的 Release 接近
# （不带 strip 的 Release 是 2.0 MB；带 strip 后约 1.7 MB）
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)   # -flto
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_link_options("-Wl,--gc-sections")        # 丢弃未引用 section
  endif()
endif()
set(CMAKE_CXX_FLAGS "-g -fno-omit-frame-pointer")
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)

# --- spdlog ----------------------------------------------------------
# 系统装好的 header-only 日志库(/usr/include/spdlog),用于写诊断/运行时日志。
# 用 header-only 模式不链 libspdlog.so,只 include + 模板实例化即可。
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
  # 回退:用项目内的 find_path 查找头文件
  find_path(SPDLOG_INCLUDE_DIR spdlog/spdlog.h PATHS /usr/include /usr/local/include)
  if(NOT SPDLOG_INCLUDE_DIR)
    message(FATAL_ERROR "spdlog not found. Install libspdlog-dev (header-only).")
  endif()
  add_library(spdlog INTERFACE)
  add_library(spdlog::spdlog_header_only ALIAS spdlog)
  target_include_directories(spdlog INTERFACE ${SPDLOG_INCLUDE_DIR})
  # spdlog 1.12 推荐用 C++20 std::format 而不是内置 fmt
  target_compile_definitions(spdlog INTERFACE SPDLOG_USE_STD_FORMAT)
endif()
# 双保险:把宏也直接传给所有 TU,确保任何 #include 顺序都生效
# (在 aicoder_core add_library 之后才设)

# --- FTXUI -----------------------------------------------------------
# ftxui 已 vendored 到 third_party/ftxui/(含 CJK 光标修复 + 删测试/示例)。
# 不走 FetchContent,直接 add_subdirectory,免网络拉取,确保 build 完全可复现。
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/ftxui)

find_package(GTest REQUIRED)
enable_testing()

# nlohmann/json: vendored 优先，系统头兜底。
# Why vendored-first: GTest in this environment injects its own bundled nlohmann/json
# (a different patch version) via -isystem. If the lib and tests end up on different
# json versions, cross-TU functions taking a json parameter fail to link (ABI mismatch).
# A project-local third_party path is a non-standard include dir that CMake will always
# emit (it strips /usr/include as implicit), so we can place it BEFORE the GTest -isystem
# and guarantee every translation unit uses the same nlohmann/json.
if(EXISTS "${PROJECT_SOURCE_DIR}/third_party/nlohmann/json.hpp")
  set(NLOHMANN_JSON_INCLUDE "${PROJECT_SOURCE_DIR}/third_party")
else()
  find_path(NLOHMANN_JSON_INCLUDE nlohmann/json.hpp PATHS /usr/include /usr/local/include)
endif()
message(STATUS "nlohmann/json include: ${NLOHMANN_JSON_INCLUDE}")

# 核心库 = src 下除 main.cpp、ui/App.cpp、ui/ReplView.cpp 外的所有 .cpp
# (main.cpp 是 TUI 程序入口;其余 UI 入口 App.cpp/ReplView.cpp/ResumePicker.cpp/
#  Welcome.cpp/PermissionDialog.cpp 通过 add_executable 单独编译)
file(GLOB_RECURSE AICODER_LIB_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM AICODER_LIB_SOURCES "${PROJECT_SOURCE_DIR}/src/main.cpp")
list(REMOVE_ITEM AICODER_LIB_SOURCES "${PROJECT_SOURCE_DIR}/src/ui/App.cpp")
list(REMOVE_ITEM AICODER_LIB_SOURCES "${PROJECT_SOURCE_DIR}/src/ui/ReplView.cpp")
list(REMOVE_ITEM AICODER_LIB_SOURCES "${PROJECT_SOURCE_DIR}/src/ui/ResumePicker.cpp")
list(REMOVE_ITEM AICODER_LIB_SOURCES "${PROJECT_SOURCE_DIR}/src/ui/Welcome.cpp")
list(REMOVE_ITEM AICODER_LIB_SOURCES "${PROJECT_SOURCE_DIR}/src/ui/MarkdownTable.cpp")

add_library(aicoder_core STATIC ${AICODER_LIB_SOURCES})
target_compile_definitions(aicoder_core PUBLIC SPDLOG_USE_STD_FORMAT)
target_include_directories(aicoder_core BEFORE PUBLIC
  "${PROJECT_SOURCE_DIR}/src"
  "${NLOHMANN_JSON_INCLUDE}"
  "${PROJECT_SOURCE_DIR}/third_party")
target_link_libraries(aicoder_core PUBLIC CURL::libcurl Threads::Threads spdlog::spdlog_header_only)
# PermissionDialog.h is included by sources compiled into aicoder_core and pulls in
# ftxui/dom/elements.hpp, so FTXUI's include path must reach aicoder_core's own TUs.
# PUBLIC propagates the include path to aicoder (the only link dependent now).
target_link_libraries(aicoder_core PUBLIC ftxui::component)

# TUI 可执行(依赖 aicoder_core + FTXUI) —— 唯一可执行入口
add_executable(aicoder "${PROJECT_SOURCE_DIR}/src/main.cpp"
                          "${PROJECT_SOURCE_DIR}/src/ui/App.cpp"
                          "${PROJECT_SOURCE_DIR}/src/ui/ReplView.cpp"
                          "${PROJECT_SOURCE_DIR}/src/ui/ResumePicker.cpp"
                          "${PROJECT_SOURCE_DIR}/src/ui/Welcome.cpp"
                          "${PROJECT_SOURCE_DIR}/src/ui/MarkdownTable.cpp")
target_link_libraries(aicoder PRIVATE aicoder_core ftxui::component)
target_include_directories(aicoder PRIVATE "${PROJECT_SOURCE_DIR}/src")

file(GLOB_RECURSE AICODER_TEST_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tests/*.cpp")
add_executable(aicoder_tests ${AICODER_TEST_SOURCES}
                 "${PROJECT_SOURCE_DIR}/src/ui/MarkdownTable.cpp")
# GTest (from this environment) injects its own bundled nlohmann/json via -isystem,
# which can differ from the system header used by aicoder_core and break the ABI on
# cross-TU functions taking a json parameter. Force the located json include first so
# every translation unit (lib + tests) uses one and the same nlohmann/json.
target_include_directories(aicoder_tests BEFORE PRIVATE "${NLOHMANN_JSON_INCLUDE}")
target_link_libraries(aicoder_tests PRIVATE aicoder_core GTest::gtest GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(aicoder_tests)
