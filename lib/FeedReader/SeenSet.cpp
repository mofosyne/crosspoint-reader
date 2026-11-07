#include "SeenSet.h"

#include <algorithm>
#include <cstring>

namespace feed {
namespace {
constexpr char MAGIC[4] = {'F', 'S', 'E', '1'};

void put64(std::string& out, uint64_t v) {
  for (int i = 0; i < 8; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
uint64_t get64(const char* p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(static_cast<unsigned char>(p[i])) << (8 * i);
  return v;
}
}  // namespace

uint64_t SeenSet::hashUrl(std::string_view url) {
  uint64_t h = 1469598103934665603ull;
  for (const unsigned char c : url) {
    h ^= c;
    h *= 1099511628211ull;
  }
  return h;
}

bool SeenSet::containsHash(uint64_t hash) const { return std::find(hashes_.begin(), hashes_.end(), hash) != hashes_.end(); }

bool SeenSet::add(std::string_view url) {
  const uint64_t h = hashUrl(url);
  if (containsHash(h)) return false;
  hashes_.push_back(h);
  if (hashes_.size() > capacity_) hashes_.erase(hashes_.begin(), hashes_.begin() + (hashes_.size() - capacity_));
  return true;
}

std::string SeenSet::serialize(int64_t lastCheck) const {
  std::string out(MAGIC, 4);
  put64(out, static_cast<uint64_t>(lastCheck));
  put64(out, hashes_.size());
  for (const uint64_t h : hashes_) put64(out, h);
  return out;
}

bool SeenSet::parse(std::string_view blob, int64_t& lastCheck) {
  if (blob.size() < 20 || memcmp(blob.data(), MAGIC, 4) != 0) return false;
  const uint64_t count = get64(blob.data() + 12);
  if (count > capacity_ * 4 || blob.size() != 20 + count * 8) return false;
  lastCheck = static_cast<int64_t>(get64(blob.data() + 4));
  hashes_.clear();
  for (uint64_t i = 0; i < count; ++i) hashes_.push_back(get64(blob.data() + 20 + i * 8));
  if (hashes_.size() > capacity_) hashes_.erase(hashes_.begin(), hashes_.begin() + (hashes_.size() - capacity_));
  return true;
}

}  // namespace feed
