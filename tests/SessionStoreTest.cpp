#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>
#include "sessions/SessionStore.h"
using namespace aicoder;
namespace fs = std::filesystem;

namespace {
fs::path scratch() {
  fs::path d = fs::temp_directory_path() / "aicoder_sessstore_test";
  fs::remove_all(d);
  return d;
}
SessionData sample(const std::string& id, const std::string& body) {
  SessionData s;
  s.id = id; s.created_at = "2026-05-30T00:00:00+08:00";
  s.updated_at = "2026-05-30T00:00:01+08:00"; s.model = "m";
  s.messages = {userText(body)};
  return s;
}
}

TEST(SessionStore, NewIdsAreUnique) {
  SessionStore store(scratch());
  std::set<std::string> ids;
  for (int i = 0; i < 100; ++i) ids.insert(store.newId());
  EXPECT_EQ(ids.size(), 100u);
}

TEST(SessionStore, SaveCreatesDirAndAtomicallyWritesFile) {
  fs::path root = scratch();
  SessionStore store(root);
  std::string id = store.newId();
  store.save(id, sample(id, "hello"));
  fs::path file = root / id / "session.json";
  EXPECT_TRUE(fs::is_regular_file(file));
  EXPECT_FALSE(fs::exists(file.string() + ".tmp"));
}

TEST(SessionStore, LoadRoundTrip) {
  fs::path root = scratch();
  SessionStore store(root);
  std::string id = store.newId();
  store.save(id, sample(id, "round-trip body"));
  auto loaded = store.load(id);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->id, id);
  ASSERT_EQ(loaded->messages.size(), 1u);
  EXPECT_EQ(assistantText(loaded->messages[0]), "round-trip body");
}

TEST(SessionStore, LoadMissingReturnsNullopt) {
  SessionStore store(scratch());
  EXPECT_FALSE(store.load("nope").has_value());
}

TEST(SessionStore, LoadCorruptJsonReturnsNullopt) {
  fs::path root = scratch();
  SessionStore store(root);
  fs::create_directories(root / "bad");
  { std::ofstream(root / "bad" / "session.json") << "not json {"; }
  EXPECT_FALSE(store.load("bad").has_value());
}

TEST(SessionStore, ListSessionsSortedByMtimeDesc) {
  fs::path root = scratch();
  SessionStore store(root);
  std::string a = store.newId();
  store.save(a, sample(a, "first"));
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  std::string b = store.newId();
  store.save(b, sample(b, "second user message here"));
  auto list = store.listSessions();
  ASSERT_EQ(list.size(), 2u);
  EXPECT_EQ(list[0].id, b);
  EXPECT_EQ(list[1].id, a);
  EXPECT_EQ(list[0].preview, "second user message here");
  EXPECT_EQ(list[0].message_count, 1u);
}

TEST(SessionStore, ListSessionsSkipsCorrupt) {
  fs::path root = scratch();
  SessionStore store(root);
  std::string good = store.newId();
  store.save(good, sample(good, "ok"));
  fs::create_directories(root / "bad_dir");
  { std::ofstream(root / "bad_dir" / "session.json") << "not json"; }
  auto list = store.listSessions();
  ASSERT_EQ(list.size(), 1u);
  EXPECT_EQ(list[0].id, good);
}

TEST(SessionStore, LatestIdMatchesNewest) {
  fs::path root = scratch();
  SessionStore store(root);
  EXPECT_FALSE(store.latestId().has_value());
  std::string a = store.newId(); store.save(a, sample(a, "x"));
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  std::string b = store.newId(); store.save(b, sample(b, "y"));
  auto latest = store.latestId();
  ASSERT_TRUE(latest.has_value());
  EXPECT_EQ(*latest, b);
}
