#pragma once

#include <cstdint>
#include <string_view>

namespace feed {

// Parses the dates feeds use: RFC 822 ("Fri, 06 Nov 2026 23:30:52 +0000", "6 Nov 2026 23:30 GMT",
// "EST" style names) and ISO 8601 / RFC 3339 ("2026-11-06T23:30:52Z", "+01:00", date only).
// Returns Unix seconds (UTC), or 0 when the text is not a date this understands.
int64_t parseFeedDate(std::string_view text);

}  // namespace feed
