#pragma once

#include <functional>
#include <list>
#include <mutex>
#include <unordered_map>

namespace aicoder
{

// FNV-1a 哈希函数
inline std::string fnv1aHash(const std::string& input) {
    static const uint64_t FNV_offset_basis = 14695981039346656037ULL;
    static const uint64_t FNV_prime = 1099511628211ULL;
    uint64_t hash = FNV_offset_basis;
    for (unsigned char c : input) {
        hash ^= static_cast<uint64_t>(c);
        hash *= FNV_prime;
    }
    char buf[20];
    snprintf(buf, sizeof(buf), "%016lx", hash);
    return std::string(buf);
}

// LRU 缓存模板类，线程安全。
// K: 键类型（需可哈希），V: 值类型。
// maxSize: 最大缓存条目数，超出时淘汰最久未使用的条目。
template <typename K, typename V> class LruCache
{
public:
  using ListType = std::list<std::pair<K, V>>;

  explicit LruCache(size_t maxSize = 128) : maxSize_(maxSize) {}

  // 查找缓存。若命中将条目移到列表头部，返回 true。
  bool get(const K &key, V *outValue)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cacheMap_.find(key);
    if (it == cacheMap_.end())
      return false;
    // 移到最前（最近使用）
    items_.splice(items_.begin(), items_, it->second);
    *outValue = it->second->second;
    return true;
  }

  // 写入缓存。若键已存在则更新并移到头部；否则插入并可能在超量时淘汰尾部。
  void put(const K &key, const V &value)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cacheMap_.find(key);
    if (it != cacheMap_.end()) {
      it->second->second = value;
      items_.splice(items_.begin(), items_, it->second);
      return;
    }
    if (items_.size() >= maxSize_) {
      // 淘汰最久未使用的尾部
      auto last = items_.back();
      cacheMap_.erase(last.first);
      items_.pop_back();
    }
    items_.emplace_front(key, value);
    cacheMap_.emplace(key, items_.begin());
  }

  // 判断键是否存在（不改变 LRU 顺序）
  bool contains(const K &key) const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return cacheMap_.find(key) != cacheMap_.end();
  }

  // 清空缓存
  void clear()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    items_.clear();
    cacheMap_.clear();
  }

  size_t size() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return items_.size();
  }

private:
  const size_t maxSize_;
  mutable std::mutex mutex_;
  std::list<std::pair<K, V>> items_; // {key, value}，前面的最"新"
  std::unordered_map<K, typename ListType::iterator> cacheMap_;
};

} // namespace aicoder