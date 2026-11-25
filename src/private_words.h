#pragma once

// Taking the user's folder, user name and computer name out of a problem report (problem_report.cpp).
// Header only, so tests/private_words_test.cpp needs nothing else.

#include <string>
#include <string_view>
#include <utility>

namespace coreoflight {

// A letter, a digit or a byte of a UTF-8 character: what a name is made of.
inline bool PrivateWordByte(char c) {
  const auto u = static_cast<unsigned char>(c);
  return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u >= 0x80;
}

// Replaces every `from` (ASCII letters in any case) that stands alone with `to`: no letter or digit
// right before or after it, at the ends where `from` itself starts or ends with one. Without that a
// short name went out inside ordinary words, and the words gave it away: a computer named "tim"
// turned a log's "frame time" into "frame %COMPUTERNAME%e". Paths ("C:\Users\tim\..."), "tim-PC",
// "tim's" and "tim_2" are still taken.
inline void ReplacePrivateWord(std::string& text, std::string_view from, std::string_view to) {
  if (from.empty()) return;
  auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; };
  const bool check_before = PrivateWordByte(from.front());
  const bool check_after = PrivateWordByte(from.back());
  std::string out;
  out.reserve(text.size());
  size_t i = 0;
  while (i < text.size()) {
    bool match = i + from.size() <= text.size();
    for (size_t k = 0; match && k < from.size(); ++k) match = lower(text[i + k]) == lower(from[k]);
    if (match && check_before && i > 0 && PrivateWordByte(text[i - 1])) match = false;
    if (match && check_after && i + from.size() < text.size() && PrivateWordByte(text[i + from.size()])) {
      match = false;
    }
    if (match) {
      out += to;
      i += from.size();
    } else {
      out += text[i++];
    }
  }
  text = std::move(out);
}

}  // namespace coreoflight
