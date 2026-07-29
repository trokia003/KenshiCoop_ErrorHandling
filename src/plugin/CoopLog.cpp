// CoopLog implementation. See CoopLog.h for rationale.
//
// VS2010 (v100) compatible: Win32 CRITICAL_SECTION + GetLocalTime, plain stdio.

#define _CRT_SECURE_NO_WARNINGS 1

#include "CoopLog.h"

#include <windows.h>
#include <cstdio>

namespace coop {
namespace {

FILE*            g_fp   = 0;
CRITICAL_SECTION g_cs;
bool             g_init = false;
char             g_tag[16] = { 0 };
volatile long    g_fakeSkewMs = 0;

// Black-box ring (CrashGuard): the last RING_N lines, kept in static storage
// so the crash handler can dump them from a dying process without touching
// the heap or the file. Written under g_cs; READ lock-free by design (a torn
// line in a crash report beats a deadlocked handler).
const unsigned int RING_N   = 64;
const unsigned int RING_LEN = 160;
char                   g_ring[RING_N][RING_LEN];
volatile unsigned long g_ringSeq = 0; // total lines ever written

void writeLine(const char* level, const char* msg) {
    if (!g_init) return;
    EnterCriticalSection(&g_cs);
    if (g_fp) {
        // Derive the stamp from wallClockMs() (real clock + injected skew) so
        // log timestamps and the wire time-sync share one clock.
        unsigned long ms = wallClockMs();
        unsigned long hh = (ms / 3600000ul) % 24ul;
        unsigned long mm = (ms / 60000ul) % 60ul;
        unsigned long ss = (ms / 1000ul) % 60ul;
        unsigned long mmm = ms % 1000ul;
        std::fprintf(g_fp, "[%02lu:%02lu:%02lu.%03lu] [%s] %s: %s\n",
                     hh, mm, ss, mmm,
                     g_tag, level, msg ? msg : "");
        std::fflush(g_fp);
        char* slot = g_ring[g_ringSeq % RING_N];
        _snprintf(slot, RING_LEN - 1, "[%02lu:%02lu:%02lu.%03lu] %s: %s",
                  hh, mm, ss, mmm, level, msg ? msg : "");
        slot[RING_LEN - 1] = '\0';
        ++g_ringSeq;
    }
    LeaveCriticalSection(&g_cs);
}

} // namespace

unsigned long wallClockMs() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    long ms = (long)((((unsigned long)st.wHour * 60ul + st.wMinute) * 60ul + st.wSecond) * 1000ul
                     + st.wMilliseconds);
    ms += g_fakeSkewMs;
    // Wrap into [0, 24h) so a skew across midnight still formats sanely.
    const long DAY = 24l * 3600l * 1000l;
    ms %= DAY;
    if (ms < 0) ms += DAY;
    return (unsigned long)ms;
}

void logSetFakeSkewMs(long skewMs) { g_fakeSkewMs = skewMs; }

void logInit(const char* path, const char* modeTag) {
    if (g_init) return;
    InitializeCriticalSection(&g_cs);
    g_init = true;

    if (modeTag) {
        size_t i = 0;
        for (; modeTag[i] && i < sizeof(g_tag) - 1; ++i) g_tag[i] = modeTag[i];
        g_tag[i] = '\0';
    }

    if (path && path[0]) {
        g_fp = std::fopen(path, "w"); // fresh file each run
    }
    writeLine("INFO", "log opened");
}

void logLine(const char* msg)    { writeLine("INFO",  msg); }
void logErrLine(const char* msg) { writeLine("ERROR", msg); }

unsigned int logRecentTo(char* out, unsigned int cap) {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!g_init) return 0;
    // Deliberately lock-free (crash context): snapshot the sequence, then copy
    // oldest -> newest. A line racing a writer may come out torn - acceptable.
    unsigned long seq = g_ringSeq;
    unsigned long count = seq < RING_N ? seq : RING_N;
    unsigned int o = 0;
    for (unsigned long i = 0; i < count; ++i) {
        const char* s = g_ring[(seq - count + i) % RING_N];
        for (unsigned int j = 0; s[j] && j < RING_LEN && o < cap - 2; ++j)
            out[o++] = s[j];
        if (o < cap - 1) out[o++] = '\n';
        if (o >= cap - 2) break;
    }
    out[o] = '\0';
    return o;
}

void logCrashLine(const char* msg) {
    if (!g_init) return;
    // TryEnter, not Enter: if the CRASHING thread already holds the log lock
    // (it faulted mid-logLine), a blocking wait would deadlock the handler.
    // The crash report file already carries everything; this line is a bonus.
    if (!TryEnterCriticalSection(&g_cs)) return;
    if (g_fp) {
        unsigned long ms = wallClockMs();
        std::fprintf(g_fp, "[%02lu:%02lu:%02lu.%03lu] [%s] FATAL: %s\n",
                     (ms / 3600000ul) % 24ul, (ms / 60000ul) % 60ul,
                     (ms / 1000ul) % 60ul, ms % 1000ul,
                     g_tag, msg ? msg : "");
        std::fflush(g_fp);
    }
    LeaveCriticalSection(&g_cs);
}

void logClose() {
    if (!g_init) return;
    EnterCriticalSection(&g_cs);
    if (g_fp) {
        std::fflush(g_fp);
        std::fclose(g_fp);
        g_fp = 0;
    }
    LeaveCriticalSection(&g_cs);
}

} // namespace coop
