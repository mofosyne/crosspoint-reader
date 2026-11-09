#include "Scheduler.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace flashcards {
namespace {

constexpr uint16_t MIN_EASE = 130;
constexpr uint16_t MAX_INTERVAL = 3650;

uint16_t clampInterval(long days) { return static_cast<uint16_t>(std::clamp<long>(days, 1, MAX_INTERVAL)); }

}  // namespace

CardState applyGrade(CardState s, Grade grade, int32_t today) {
  s.seen = true;
  switch (grade) {
    case Grade::Again:
      ++s.lapses;
      s.reps = 0;
      s.interval = 0;
      s.ease = static_cast<uint16_t>(std::max<int>(MIN_EASE, s.ease - 20));
      s.due = today;
      return s;
    case Grade::Hard:
      s.interval = clampInterval(s.interval == 0 ? 1 : (s.interval * 12L + 5) / 10);
      s.ease = static_cast<uint16_t>(std::max<int>(MIN_EASE, s.ease - 15));
      break;
    case Grade::Good:
      s.interval = clampInterval(s.reps == 0 ? 1 : s.reps == 1 ? 3 : (s.interval * static_cast<long>(s.ease) + 50) / 100);
      break;
    case Grade::Easy:
      s.interval = clampInterval(s.reps == 0 ? 4 : (s.interval * static_cast<long>(s.ease) * 13L + 500) / 1000);
      s.ease = static_cast<uint16_t>(std::min<int>(400, s.ease + 15));
      break;
  }
  ++s.reps;
  s.due = today + s.interval;
  return s;
}

int daysUntil(const CardState& state, Grade grade, int32_t today) {
  return applyGrade(state, grade, today).due - today;
}

bool isDue(const CardState& state, int32_t today) { return state.seen && state.due <= today; }

std::vector<size_t> studyQueue(const std::vector<CardState>& states, int32_t today, size_t newLimit) {
  std::vector<size_t> due;
  std::vector<size_t> fresh;
  for (size_t i = 0; i < states.size(); ++i) {
    if (isDue(states[i], today)) due.push_back(i);
    else if (!states[i].seen && fresh.size() < newLimit) fresh.push_back(i);
  }
  std::stable_sort(due.begin(), due.end(), [&](size_t a, size_t b) { return states[a].due < states[b].due; });
  due.insert(due.end(), fresh.begin(), fresh.end());
  return due;
}

std::string serializeProgress(const std::vector<CardState>& states) {
  std::string out;
  char line[80];
  for (const auto& s : states) {
    if (!s.seen) continue;
    snprintf(line, sizeof(line), "%08x %d %u %u %u %u\n", static_cast<unsigned>(s.key), static_cast<int>(s.due),
             s.interval, s.ease, s.reps, s.lapses);
    out += line;
  }
  return out;
}

void applyProgress(std::string_view text, std::vector<CardState>& states) {
  size_t pos = 0;
  while (pos < text.size()) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string_view::npos) eol = text.size();
    const std::string line(text.substr(pos, eol - pos));
    pos = eol + 1;
    unsigned key = 0, interval = 0, ease = 0, reps = 0, lapses = 0;
    int due = 0;
    if (sscanf(line.c_str(), "%x %d %u %u %u %u", &key, &due, &interval, &ease, &reps, &lapses) != 6) continue;
    for (auto& s : states) {
      if (s.key != key) continue;
      s.due = due;
      s.interval = static_cast<uint16_t>(interval);
      s.ease = static_cast<uint16_t>(std::max<unsigned>(ease, MIN_EASE));
      s.reps = static_cast<uint16_t>(reps);
      s.lapses = static_cast<uint16_t>(lapses);
      s.seen = true;
      break;
    }
  }
}

}  // namespace flashcards
