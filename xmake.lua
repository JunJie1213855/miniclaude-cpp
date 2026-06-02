-- AICoder xmake build (standalone, no CMake dependency)
set_languages("cxx20")

-- 自动下载 nlohmann_json
-- Release 模式优化
if is_mode("release") then
    set_strip("all")                      -- 剥离所有符号（减小二进制体积）
    set_optimize("fastest")               -- 最高级别优化
end

-- FTXUI 子模块（从 third_party/ftxui 编译）
target("ftxui-screen")
    set_kind("static")
    add_includedirs("third_party/ftxui/include", {public = true})
    add_includedirs("third_party/ftxui/src", {public = true})
    add_files("third_party/ftxui/src/ftxui/screen/*.cpp")

target("ftxui-dom")
    set_kind("static")
    add_includedirs("third_party/ftxui/include", {public = true})
    add_includedirs("third_party/ftxui/src", {public = true})
    add_files("third_party/ftxui/src/ftxui/dom/*.cpp")
    add_deps("ftxui-screen")

target("ftxui-component")
    set_kind("static")
    add_includedirs("third_party/ftxui/include", {public = true})
    add_includedirs("third_party/ftxui/src", {public = true})
    add_files("third_party/ftxui/src/ftxui/component/*.cpp")
    add_deps("ftxui-dom", "ftxui-screen")

-- 核心库（不包含 main 和 UI 入口）
target("aicoder_core")
    set_kind("static")
    set_languages("cxx20")
    add_includedirs("src", {public = true})
    add_includedirs("third_party/nlohmann", {public = true})
    add_files("src/commands/*.cpp")
    add_files("src/config/*.cpp")
    add_files("src/core/*.cpp")
    add_files("src/llm/*.cpp")
    add_files("src/rules/*.cpp")
    add_files("src/sessions/*.cpp")
    add_files("src/skills/*.cpp")
    add_files("src/tools/*.cpp")
    add_files("src/ui/ConsoleRepl.cpp")
    add_files("src/workspace/*.cpp")
    add_syslinks("curl", "pthread")

-- TUI 可执行文件
target("aicoder")
    set_kind("binary")
    add_files("src/main.cpp",
             "src/ui/App.cpp",
             "src/ui/ReplView.cpp",
             "src/ui/ResumePicker.cpp",
             "src/ui/Welcome.cpp",
             "src/ui/PermissionDialog.cpp")
    add_deps("aicoder_core", "ftxui-component")
    add_includedirs("src", "third_party/nlohmann")
    add_syslinks("curl", "pthread")

-- 测试
target("aicoder_tests")
    set_kind("binary")
    add_files("tests/*.cpp")
    add_deps("aicoder_core")
    add_includedirs("src", "third_party/nlohmann")
    add_syslinks("gtest", "gmock", "pthread")