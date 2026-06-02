#pragma once
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aicoder
{
    // 读文件内容；不存在/不可读返回 nullopt。
    std::optional<std::string> readFile(const std::filesystem::path &p);
    // 递归收集 dir 下所有 *.md，按路径排序；目录不存在返回空。
    std::vector<std::filesystem::path> listMarkdown(const std::filesystem::path &dir);
    // $HOME/.aicoder（无 HOME 返回空 path）。
    std::filesystem::path globalDir();

    // 解析 markdown 顶部 frontmatter（--- 块内的 key: value），返回元数据 + 去掉 frontmatter 的正文。
    struct Frontmatter
    {
        std::map<std::string, std::string> meta;
        std::string body;
    };
    Frontmatter parseFrontmatter(const std::string &content);
}
