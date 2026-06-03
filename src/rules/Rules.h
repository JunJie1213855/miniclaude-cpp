#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace aicoder
{
    extern const char *const kBaseSystemPrompt;
    std::string concatSources(const std::vector<std::filesystem::path> &sources);
    std::string loadRules(const std::filesystem::path &globalDir,
                          const std::filesystem::path &projectRoot);
    std::string buildSystemPrompt(const std::string &base, const std::string &rules,
                                  const std::string &skillList);
}
