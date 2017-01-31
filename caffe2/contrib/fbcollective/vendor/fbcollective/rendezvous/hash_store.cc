#include "fbcollective/rendezvous/hash_store.h"

namespace fbcollective {
namespace rendezvous {

void HashStore::set(const std::string& key, const std::vector<char>& data) {
  std::unique_lock<std::mutex> lock(m_);
  map_[key] = data;
  cv_.notify_all();
}

std::vector<char> HashStore::get(const std::string& key) {
  std::unique_lock<std::mutex> lock(m_);
  auto it = map_.find(key);
  if (it == map_.end()) {
    return std::vector<char>();
  }

  return it->second;
}

void HashStore::wait(const std::vector<std::string>& keys) {
  std::unique_lock<std::mutex> lock(m_);
  for (;;) {
    auto wait = false;
    for (const auto& key : keys) {
      if (map_.find(key) == map_.end()) {
        wait = true;
        break;
      }
    }

    if (!wait) {
      return;
    }

    cv_.wait(lock);
  }
}

} // namespace rendezvous
} // namespace fbcollective
