#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>
namespace aicoder
{
  struct CommandTemplate
  {
    std::string name;
    std::string description;
    std::string body;
  };
  class CommandRegistry
  {
  public:
    void discover(const std::filesystem::path &globalDir,
                  const std::filesystem::path &projectDir);
    void addOne(const std::filesystem::path &commandMdFile);
    void addOne(const CommandTemplate &t) { templates_[t.name] = t; }
    const CommandTemplate *find(const std::string &name) const;
    std::vector<CommandTemplate> list() const;

  private:
    std::map<std::string, CommandTemplate> templates_;
  };
}
