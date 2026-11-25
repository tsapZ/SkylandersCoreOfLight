// What a game's exit code means, for the launcher's log line and its crash box. On Windows the code
// is an NTSTATUS; on Linux a game killed by a signal ends with 128 + the signal's number
// (platform.cpp IsProcessRunning), which said nothing as a bare 0x00000087 (Core of Light #135: a
// bus error, /dev/shm full of older game versions' memory).
//
// on_linux: the code came from a Linux process (the numbers are Linux's: SIGBUS is 10 on macOS).
// Not "linux": GCC's GNU modes define that as a macro.
#pragma once

namespace coreoflight {

struct ExitSignal {
  unsigned long code;  // 128 + the signal's number
  const char* name;
  const char* kind;  // for the log line, after "exit code 0x..."
};

inline constexpr ExitSignal kExitSignals[] = {
    {128 + 4, "SIGILL", ", an illegal instruction (SIGILL)"},
    {128 + 6, "SIGABRT", ", an abort (SIGABRT)"},
    {128 + 7, "SIGBUS", ", a bus error (SIGBUS)"},
    {128 + 8, "SIGFPE", ", a floating point error (SIGFPE)"},
    {128 + 9, "SIGKILL", ", killed (SIGKILL, often the system out of memory)"},
    {128 + 11, "SIGSEGV", ", an access violation (SIGSEGV)"},
};

// The signal's name ("SIGBUS"), or "" when the code isn't one of them.
inline const char* ExitCodeSignal(unsigned long code, bool on_linux) {
  if (on_linux) {
    for (const ExitSignal& s : kExitSignals) {
      if (s.code == code) return s.name;
    }
  }
  return "";
}

// ", an access violation" and the like, to put after "exit code 0x...", or "".
inline const char* ExitCodeKind(unsigned long code, bool on_linux) {
  if (!on_linux) {
    return code == 0xC0000005   ? ", an access violation"
           : code == 0xC0000409 ? ", a stack buffer overrun or fast fail"
           : code == 0xC0000135 ? ", a DLL missing"
                                : "";
  }
  for (const ExitSignal& s : kExitSignals) {
    if (s.code == code) return s.kind;
  }
  return "";
}

// The bus error's usual cause on Linux, for the crash box: game versions before 2026-11-26 left
// their memory in /dev/shm after every run, and a full /dev/shm stops the next start at once.
inline bool ExitCodeIsBusError(unsigned long code, bool on_linux) { return on_linux && code == 128 + 7; }

// This build's own processes: Linux or not.
#if defined(__linux__)
inline constexpr bool kLinuxBuild = true;
#else
inline constexpr bool kLinuxBuild = false;
#endif

}  // namespace coreoflight
