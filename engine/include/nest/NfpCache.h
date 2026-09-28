#pragma once
// Memoises no-fit polygons. A sheet of 500 identical stickers needs one NFP,
// not 500, because the shape only depends on which designs and which rotations
// are involved, never on where a piece ended up. Positions are applied by
// translating the cached result.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "nest/Types.h"

namespace nest {

struct NfpKey {
  int designA{0};
  int rotationA{0};  // index into the rotation table, not degrees
  int designB{0};
  int rotationB{0};

  bool operator==(const NfpKey& other) const = default;
};

struct NfpKeyHash {
  std::size_t operator()(const NfpKey& k) const noexcept {
    std::uint64_t h = static_cast<std::uint32_t>(k.designA);
    h = h * 0x9E3779B97F4A7C15ull + static_cast<std::uint32_t>(k.rotationA);
    h = h * 0x9E3779B97F4A7C15ull + static_cast<std::uint32_t>(k.designB);
    h = h * 0x9E3779B97F4A7C15ull + static_cast<std::uint32_t>(k.rotationB);
    return static_cast<std::size_t>(h);
  }
};

// Safe to share between search workers. Entries are held behind a pointer so a
// rehash never moves what a caller is already reading, and `build` runs outside
// the lock so one slow no-fit polygon cannot stall every other thread. Two
// threads racing on the same key may both build it; that wastes a little work
// and is far cheaper than serialising them.
class NfpCache {
 public:
  // Returns the cached entry, building it with `build` on a miss.
  const Polygons& get(const NfpKey& key, const std::function<Polygons()>& build);

  std::size_t size() const;
  std::size_t hits() const { return hits_.load(std::memory_order_relaxed); }
  std::size_t misses() const { return misses_.load(std::memory_order_relaxed); }
  void clear();

 private:
  mutable std::mutex mutex_;
  std::unordered_map<NfpKey, std::unique_ptr<Polygons>, NfpKeyHash> entries_;
  std::atomic<std::size_t> hits_{0};
  std::atomic<std::size_t> misses_{0};
};

}  // namespace nest
