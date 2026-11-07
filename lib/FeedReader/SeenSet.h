#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace feed {

// The URLs a source has already delivered, kept as 64-bit hashes (8 bytes each instead of a whole
// URL). Oldest entries are dropped past `capacity`. Serialises to a small binary blob for the SD card.
class SeenSet {
 public:
  explicit SeenSet(size_t capacity = 4096) : capacity_(capacity) {}

  static uint64_t hashUrl(std::string_view url);  // FNV-1a over the URL as written

  bool contains(std::string_view url) const { return containsHash(hashUrl(url)); }
  bool containsHash(uint64_t hash) const;
  // Adds the URL; false when it was already present.
  bool add(std::string_view url);
  size_t size() const { return hashes_.size(); }

  std::string serialize(int64_t lastCheck) const;
  // False for a blob this version does not understand; `lastCheck` is set from it.
  bool parse(std::string_view blob, int64_t& lastCheck);

 private:
  size_t capacity_;
  std::vector<uint64_t> hashes_;  // oldest first
};

}  // namespace feed
