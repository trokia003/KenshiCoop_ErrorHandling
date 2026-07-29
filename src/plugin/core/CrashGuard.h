// CrashGuard - process-wide last-chance crash recorder.
//
// The engine facade already SEH-guards every engine call the PLUGIN makes
// (Engine.h: "a transient bad pointer must not take down the game"), but a
// fault on one of the GAME's own threads hits no guard at all:
// the process dies silently and the log just stops.
// This module is the missing last line:
//
//   * SetUnhandledExceptionFilter at plugin load - fires for ANY unhandled
//     fault on ANY thread, plugin or engine.
//   * On crash, using only crash-safe primitives (static buffers, raw
//     WriteFile, no CRT heap): append a report to KenshiCoop_crash.txt -
//     exception code, faulting module+offset (the one fact that settles
//     "engine bug or co-op bug"), registers, a module-resolved stack scan,
//     and the black-box ring of recent log lines (CoopLog).
//   * Write a minidump (KenshiCoop_crash.dmp via dbghelp MiniDumpWriteDump,
//     resolved dynamically at install so no import-lib change) - openable in
//     WinDbg for a full post-mortem without any user-side WER setup.
//   * Chain to the previous filter (WER / other tools still run).
//   * At the NEXT launch, install() notices the artifacts, logs a one-line
//     "[crash] PREVIOUS session ended in a crash: ..." summary into the new
//     session's log, and renames both files to *_prev so each crash gets a
//     fresh pair (two generations kept).
//

#ifndef KENSHICOOP_CRASHGUARD_H
#define KENSHICOOP_CRASHGUARD_H

namespace coop {
namespace crashguard {

// Install the filter. 'logPath' is the coop log's path - the crash report and
// minidump are written next to it (same directory; a bare filename lands in
// the game's working directory, exactly like the log itself). 'modeTag' is
// the HOST/JOIN tag stamped into the report header. Call once, right after
// logInit, BEFORE any hooks are installed. Safe to call again (no-op).
void install(const char* logPath, const char* modeTag);

} // namespace crashguard
} // namespace coop

#endif // KENSHICOOP_CRASHGUARD_H
