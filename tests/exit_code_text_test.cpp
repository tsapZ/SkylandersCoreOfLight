// exit_code_text.h's tests: a Linux game killed by a signal gets the signal's name, Windows codes
// keep their words, and a Windows code with a Linux-looking number gets none.

#include <cstdio>
#include <cstring>

#include "exit_code_text.h"

using coreoflight::ExitCodeIsBusError;
using coreoflight::ExitCodeKind;
using coreoflight::ExitCodeSignal;

static int g_failures = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

static bool Is(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

int main() {
  // Linux: 128 + the signal (#135: 0x87, a bus error).
  CHECK(Is(ExitCodeSignal(0x87, true), "SIGBUS"));
  CHECK(Is(ExitCodeKind(0x87, true), ", a bus error (SIGBUS)"));
  CHECK(Is(ExitCodeSignal(0x86, true), "SIGABRT"));
  CHECK(Is(ExitCodeSignal(0x8B, true), "SIGSEGV"));
  CHECK(Is(ExitCodeSignal(0x89, true), "SIGKILL"));
  CHECK(ExitCodeIsBusError(0x87, true));
  // An ordinary exit code on Linux says nothing.
  CHECK(Is(ExitCodeSignal(1, true), ""));
  CHECK(Is(ExitCodeKind(2, true), ""));
  CHECK(!ExitCodeIsBusError(0x86, true));
  // Windows: the NTSTATUS words, and no signal names for the same numbers.
  CHECK(Is(ExitCodeKind(0xC0000005, false), ", an access violation"));
  CHECK(Is(ExitCodeKind(0xC0000135, false), ", a DLL missing"));
  CHECK(Is(ExitCodeSignal(0x87, false), ""));
  CHECK(Is(ExitCodeKind(0x87, false), ""));
  CHECK(!ExitCodeIsBusError(0x87, false));
  // An NTSTATUS on Linux is no signal either.
  CHECK(Is(ExitCodeKind(0xC0000005, true), ""));

  if (g_failures) {
    std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("exit_code_text: all checks passed\n");
  return 0;
}
