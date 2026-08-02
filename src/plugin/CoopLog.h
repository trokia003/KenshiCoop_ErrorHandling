// CoopLog - tiny thread-safe file logger for KenshiCoop.
//
// Why a separate logger when KenshiLib already has DebugLog/ErrorLog?
//   * Those route into the engine's kenshi.log, which is overwritten each
//     launch and interleaved with heavy engine spam - awkward for an automated
//     test runner to evaluate.
//   * KenshiLib's UI/log helpers are NOT safe to call off the main thread,
//     whereas this logger guards a FILE* with a CRITICAL_SECTION so the net
//     thread can write too.
//
// Output is a dedicated, per-line-flushed file (so it survives a hard kill),
// with a timestamp + mode tag (HOST/JOIN) on every line. The high-level
// coopLog()/coopErr() wrappers in KenshiCoop.cpp call BOTH this and the
// KenshiLib helpers, so events still appear in kenshi.log as before.

#ifndef KENSHICOOP_COOPLOG_H
#define KENSHICOOP_COOPLOG_H

namespace coop {

// Open the log file at 'path' (truncating any previous run) and remember a
// short mode tag (e.g. "HOST"/"JOIN"). Safe to call once at plugin load.
void logInit(const char* path, const char* modeTag);

// The wall clock every timestamp in this plugin derives from: milliseconds
// since local midnight PLUS the injected fake skew (see logSetFakeSkewMs).
// Both the log-line "[HH:MM:SS.mmm]" stamps AND the wire time-sync packets
// (NetLink CLOCKSYNC) read THIS function, so an injected skew shifts them
// together - which is exactly what the clock-skew validation relies on: the
// join's log stamps drift by +S while its estimated host-offset reads -S, and
// the oracles' offset correction must recover alignment. Thread-safe.
unsigned long wallClockMs();

// Inject a fake wall-clock skew (ms, may be negative). Set once at startup from
// KENSHICOOP_FAKE_CLOCK_SKEW_MS (join only) BEFORE logInit. 0 = real clock.
void logSetFakeSkewMs(long skewMs);

// Append one INFO/ERROR line (timestamped, tagged) and flush. Thread-safe.
void logLine(const char* msg);
void logErrLine(const char* msg);

// Re-tag the log at a role flip (the F2 panel picks host/join AFTER logInit
// baked the config default - a panel-JOIN client stamped and NAMED everything
// HOST). Later lines carry the new tag; segment files opened from now on are
// named with it too. Thread-safe.
void logSetTag(const char* modeTag);

// CrashGuard black box: copy the last ~64 log lines (oldest first, newline-
// separated, NUL-terminated) into 'out'. Returns bytes written. Lock-FREE by
// design - safe to call from a crash handler; a torn line is acceptable.
unsigned int logRecentTo(char* out, unsigned int cap);

// Crash-context FATAL line: like logErrLine but TryEnterCriticalSection - a
// crashing thread that already holds the log lock skips instead of deadlocking.
void logCrashLine(const char* msg);

// ---- MP log segmentation (v48 log shipping) ---------------------------------
// Mirror every line into <dir>\KenshiCoop_<tag>_<yyyymmdd_hhmmss>.log, rolling
// to a fresh segment every segMinutes and pruning the directory to maxFiles
// (oldest first; shipped *.sent copies count toward the cap). The MAIN log file
// is untouched - the harness oracles parse it. Call once after logInit; the
// segment writer shares the log lock, so it is exactly as thread-safe as
// logLine. A crash leaves the current segment on disk (per-line flush) where
// the next session's unshipped scan finds it.
void logSegmentsInit(const char* dir, unsigned int segMinutes, unsigned int maxFiles);

// MAIN thread: drain full paths of segments CLOSED by rotation since the last
// call (each is finished and shippable). Returns the count written to out.
unsigned int logTakeFinishedSegments(char out[][512], unsigned int maxOut);

// The ACTIVE segment's full path ("" when segmentation is off) - shippers must
// skip it (still being written).
void logCurrentSegment(char* out, unsigned int cap);

// Flush and close the file. Called right before ExitProcess on test self-exit.
void logClose();

} // namespace coop

#endif // KENSHICOOP_COOPLOG_H
