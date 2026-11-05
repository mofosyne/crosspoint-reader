#pragma once

#include <stdint.h>

#include <string_view>

// Single-range "Range: bytes=..." parsing (RFC 7233) for static file responses.
namespace httprange {

enum class Kind {
  Full,           // no usable Range header: serve the whole file with 200
  Partial,        // serve [start, end] with 206
  Unsatisfiable,  // serve 416
};

struct Result {
  Kind kind = Kind::Full;
  uint64_t start = 0;
  uint64_t end = 0;  // inclusive, valid for Partial
};

namespace detail {
// Digits only, false on empty or overflow.
inline bool parseNumber(std::string_view s, uint64_t& out) {
  if (s.empty()) return false;
  uint64_t v = 0;
  for (const char c : s) {
    if (c < '0' || c > '9') return false;
    const uint64_t digit = static_cast<uint64_t>(c - '0');
    if (v > (UINT64_MAX - digit) / 10) return false;
    v = v * 10 + digit;
  }
  out = v;
  return true;
}

inline std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}
}  // namespace detail

// Unsupported units, multiple ranges and malformed values are ignored (Full),
// which RFC 7233 permits. A zero-length file has no satisfiable range.
inline Result parse(std::string_view header, uint64_t size) {
  Result r;
  header = detail::trim(header);
  constexpr std::string_view UNIT = "bytes=";
  if (header.size() < UNIT.size() || header.substr(0, UNIT.size()) != UNIT) return r;
  const std::string_view spec = detail::trim(header.substr(UNIT.size()));
  if (spec.find(',') != std::string_view::npos) return r;
  const size_t dash = spec.find('-');
  if (dash == std::string_view::npos) return r;
  const std::string_view first = detail::trim(spec.substr(0, dash));
  const std::string_view last = detail::trim(spec.substr(dash + 1));

  uint64_t start = 0;
  uint64_t end = 0;
  if (first.empty()) {
    // Suffix form "-N": the last N bytes.
    uint64_t suffix = 0;
    if (!detail::parseNumber(last, suffix)) return r;
    if (suffix == 0 || size == 0) {
      r.kind = Kind::Unsatisfiable;
      return r;
    }
    start = suffix >= size ? 0 : size - suffix;
    end = size - 1;
  } else {
    if (!detail::parseNumber(first, start)) return r;
    if (last.empty()) {
      end = size == 0 ? 0 : size - 1;
    } else {
      if (!detail::parseNumber(last, end) || end < start) return r;
      if (size != 0 && end >= size) end = size - 1;
    }
    if (start >= size) {
      r.kind = Kind::Unsatisfiable;
      return r;
    }
  }
  r.kind = Kind::Partial;
  r.start = start;
  r.end = end;
  return r;
}

// Last byte to send so the body is at most maxLen bytes starting at `start`.
inline uint64_t clampEnd(uint64_t start, uint64_t end, uint64_t maxLen) {
  return end - start >= maxLen ? start + (maxLen - 1) : end;
}

}  // namespace httprange
