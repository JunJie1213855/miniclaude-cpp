#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include "sessions/Session.h"

namespace aicoder {

struct SessionInfo {
  std::string id;
  std::string updated_at;
  std::string model;
  std::string preview;        // first user message, trimmed to <=60 chars; "" if none
  size_t message_count = 0;
  std::filesystem::file_time_type mtime;
};

class SessionStore {
public:
  // root defaults to ~/.aicoder/sessions; root is created if missing.
  explicit SessionStore(std::filesystem::path root);

  std::string newId() const;
  void save(const std::string& id, const SessionData& data);
  std::optional<SessionData> load(const std::string& id);
  std::vector<SessionInfo> listSessions() const;
  std::optional<std::string> latestId() const;
  // 删除会话目录，返回 true 表示成功删除。
  bool remove(const std::string& id);

  const std::filesystem::path& root() const { return root_; }

private:
  std::filesystem::path root_;
  std::filesystem::path fileFor(const std::string& id) const { return root_ / id / "session.json"; }
};

}  // namespace aicoder
