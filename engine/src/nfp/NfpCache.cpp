#include "nest/NfpCache.h"

namespace nest {

const Polygons& NfpCache::get(const NfpKey& key,
                              const std::function<Polygons()>& build) {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = entries_.find(key);
    if (it != entries_.end()) {
      hits_.fetch_add(1, std::memory_order_relaxed);
      return *it->second;
    }
  }

  // Built with the lock released: a no-fit polygon can take tens of
  // milliseconds and holding the mutex for that would serialise the search.
  Polygons built = build();

  const std::lock_guard<std::mutex> lock(mutex_);
  const auto it = entries_.find(key);
  if (it != entries_.end()) {
    // Another worker got there first. Its copy is as good as ours.
    hits_.fetch_add(1, std::memory_order_relaxed);
    return *it->second;
  }
  misses_.fetch_add(1, std::memory_order_relaxed);
  return *entries_
              .emplace(key, std::make_unique<Polygons>(std::move(built)))
              .first->second;
}

std::size_t NfpCache::size() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return entries_.size();
}

void NfpCache::clear() {
  const std::lock_guard<std::mutex> lock(mutex_);
  entries_.clear();
  hits_.store(0, std::memory_order_relaxed);
  misses_.store(0, std::memory_order_relaxed);
}

}  // namespace nest
