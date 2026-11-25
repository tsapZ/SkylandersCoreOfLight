// private_words.h's tests: a problem report takes a private name out only where it stands alone, so
// the words around can't give a short name away.

#include <cstdio>
#include <string>

#include "private_words.h"

using coreoflight::ReplacePrivateWord;

static int g_failures = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

static std::string Clean(std::string text) {
  ReplacePrivateWord(text, "C:\\Users\\tim", "%USERPROFILE%");
  ReplacePrivateWord(text, "tim", "%USERNAME%");
  return text;
}

int main() {
  // Inside ordinary words the name stays (it was "frame %USERNAME%e" before).
  CHECK(Clean("frame time 16 ms, timer TIMED") == "frame time 16 ms, timer TIMED");
  CHECK(Clean("Timothy Victim") == "Timothy Victim");
  // Where it stands alone it goes, in any case.
  CHECK(Clean("tim") == "%USERNAME%");
  CHECK(Clean("user = TIM\n") == "user = %USERNAME%\n");
  CHECK(Clean("TIM-PC, tim's, tim_2, (tim)") == "%USERNAME%-PC, %USERNAME%'s, %USERNAME%_2, (%USERNAME%)");
  // The folder first, and the name after it in a path.
  CHECK(Clean("\"C:\\Users\\tim\\Documents\"") == "\"%USERPROFILE%\\Documents\"");
  CHECK(Clean("D:\\Games\\tim\\x") == "D:\\Games\\%USERNAME%\\x");
  // A name that starts or ends with no letter needs no gap there.
  std::string home = "x=/home/tim/Documents";
  ReplacePrivateWord(home, "/home/tim", "$HOME");
  CHECK(home == "x=$HOME/Documents");
  std::string glued = "path/home/tim";
  ReplacePrivateWord(glued, "/home/tim", "$HOME");
  CHECK(glued == "path$HOME");
  // Nothing to take: the text stays.
  std::string empty_word = "tim";
  ReplacePrivateWord(empty_word, "", "x");
  CHECK(empty_word == "tim");

  if (g_failures) {
    std::fprintf(stderr, "%d private_words check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("all private_words tests passed\n");
  return 0;
}
