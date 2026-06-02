#include "sessions/SessionStore.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace aicoder {
namespace fs = std::filesystem;

namespace {
std::string fourHex() {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dist(0, 0xFFFF);
  std::ostringstream os;
  os << std::hex << std::setw(4) << std::setfill('0') << dist(gen);
  return os.str();
}
}  // namespace

SessionStore::SessionStore(fs::path root) : root_(std::move(root)) {
  std::error_code ec;
  fs::create_directories(root_, ec);
}

std::string SessionStore::newId() const {
  using namespace std::chrono;
  std::time_t t = system_clock::to_time_t(system_clock::now());
  std::tm tm{};
  localtime_r(&t, &tm);
  std::ostringstream os;
  os << std::put_time(&tm, "%Y%m%d_%H%M%S") << "_" << fourHex();
  return os.str();
}

void SessionStore::save(const std::string& id, const SessionData& data) {
  std::error_code ec;
  fs::path dir = root_ / id;
  fs::create_directories(dir, ec);
  fs::path file = dir / "session.json";
  fs::path tmp = file;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out << sessionToJson(data).dump(2);
  }
  fs::rename(tmp, file, ec);
  if (ec) {
    fs::remove(tmp, ec);
  }
}

std::optional<SessionData> SessionStore::load(const std::string& id) {
  std::error_code ec;
  fs::path file = fileFor(id);
  if (!fs::is_regular_file(file, ec)) return std::nullopt;
  std::ifstream in(file, std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream ss; ss << in.rdbuf();
  json j;
  try { j = json::parse(ss.str()); }
  catch (const json::exception&) { return std::nullopt; }
  return sessionFromJson(j);
}

std::vector<SessionInfo> SessionStore::listSessions() const {
  std::vector<SessionInfo> out;
  std::error_code ec;
  if (!fs::is_directory(root_, ec)) return out;
  for (auto it = fs::directory_iterator(root_, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::directory_iterator(); it.increment(ec)) {
    if (!it->is_directory(ec)) continue;
    fs::path file = it->path() / "session.json";
    if (!fs::is_regular_file(file, ec)) continue;
    std::ifstream in(file, std::ios::binary);
    if (!in) continue;
    std::ostringstream ss; ss << in.rdbuf();
    json j;
    try { j = json::parse(ss.str()); } catch (const json::exception&) { continue; }
    auto data = sessionFromJson(j);
    if (!data) continue;
    SessionInfo info;
    info.id = data->id;
    info.updated_at = data->updated_at;
    info.model = data->model;
    info.message_count = data->messages.size();
    for (const auto& m : data->messages) {
      if (m.role == Role::User) {
        std::string txt = assistantText(m);
        if (txt.size() > 60) txt.resize(60);
        info.preview = txt;
        break;
      }
    }
    info.mtime = fs::last_write_time(file, ec);
    out.push_back(std::move(info));
  }
  std::sort(out.begin(), out.end(),
            [](const SessionInfo& a, const SessionInfo& b) { return a.mtime > b.mtime; });
  return out;
}

std::optional<std::string> SessionStore::latestId() const {
  auto list = listSessions();
  if (list.empty()) return std::nullopt;
  return list.front().id;
}

bool SessionStore::remove(const std::string& id) {
  std::error_code ec;
  fs::path dir = root_ / id;
  if (!fs::is_directory(dir, ec)) return false;
  fs::remove_all(dir, ec);
  return !ec;
}

}  // namespace aicoder
