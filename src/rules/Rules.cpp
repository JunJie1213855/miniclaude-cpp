#include "rules/Rules.h"
#include "workspace/Workspace.h"

namespace aicoder
{
  namespace fs = std::filesystem;

  const char *const kBaseSystemPrompt = R"(
你是 AICoder，一个运行在命令行的编码助手。

## 可用工具
- read_file: 读取文件内容
- list_dir: 列出目录
- glob: 搜索文件
- grep: 搜索代码
- write_file: 写入文件（需要权限）
- edit_file: 编辑文件（需要权限）
- create_file: 新建文件，已存在则失败（需要权限）
- create_dir: 新建目录（需要权限）
- delete_file: 删除文件（需要权限）
- move_file: 移动/重命名文件或目录（需要权限）
- bash: 执行 shell 命令，如编译/测试/git（需要权限）

## 代码格式要求
输出代码时必须遵循 Google C++ Style Guide：
- 行宽不超过 80 字符
- 大括号独立一行
- if/while/for 等结构：
  if (condition) {
    doSomething();
  }
- 函数返回值类型单独一行
- 命名空间不缩进
- using 独立成行
- 头文件按字母排序
- 成员变量用 m_ 前缀或下划线后缀

## 代码示例
正确：
```cpp
std::vector<std::string> calculateResults(
    const std::string& input,
    int flags) {
  if (input.empty()) {
    return {};
  }
  std::vector<std::string> results;
  for (const auto& item : items) {
    if (item.isValid()) {
      results.push_back(item.name());
    }
  }
  return results;
}
```

错误（不要这样写）：
```cpp
std::vector<std::string> calculateResults(const std::string& input, int flags) {
  if (input.empty()) { return {}; }
  std::vector<std::string> results;
  for (const auto& item : items) { if (item.isValid()) { results.push_back(item.name()); } }
  return results;
}
```
)";

  std::string concatSources(const std::vector<fs::path> &sources)
  {
    std::string out;
    auto append = [&](const std::string &s)
    {
      if (s.empty())
        return;
      if (!out.empty())
        out += "\n\n";
      out += s;
    };
    for (const auto &src : sources)
    {
      std::error_code ec;
      if (fs::is_directory(src, ec))
      {
        for (const auto &md : listMarkdown(src))
          if (auto c = readFile(md))
            append(*c);
      }
      else if (auto c = readFile(src))
      {
        append(*c);
      }
    }
    return out;
  }

  std::string loadRules(const fs::path &globalDir, const fs::path &projectRoot)
  {
    return concatSources({
        globalDir / "CLAUDE.md",
        globalDir / "rules",
        projectRoot / "CLAUDE.md",
        projectRoot / ".aicoder" / "rules",
    });
  }

  std::string buildSystemPrompt(const std::string &base, const std::string &rules,
                                const std::string &skillList)
  {
    std::string out = base;
    if (!rules.empty())
      out += "\n\n" + rules;
    if (!skillList.empty())
      out += "\n\n## 可用技能\n" + skillList;
    return out;
  }
} // namespace aicoder
