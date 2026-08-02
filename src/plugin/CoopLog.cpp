// CoopLog implementation. See CoopLog.h for rationale.
//
// VS2010 (v100) compatible: Win32 CRITICAL_SECTION + GetLocalTime, plain stdio.

#define _CRT_SECURE_NO_WARNINGS 1

#include "CoopLog.h"

#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

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

// MP log segmentation (v48 log shipping). All state guarded by g_cs; rotation
// happens inside writeLine so no separate timer thread exists.
FILE*         g_segFp        = 0;
char          g_segDir[400]  = { 0 };
char          g_segPath[512] = { 0 };
unsigned long g_segStart     = 0;  // GetTickCount at segment open
unsigned int  g_segMinutes   = 15;
unsigned int  g_segMaxFiles  = 100;
bool          g_segOn        = false;
const unsigned int SEG_DONE_MAX = 8; // finished-segment handoff ring
char          g_segDone[SEG_DONE_MAX][512];
unsigned int  g_segDoneN     = 0;

// Prune <dir> to maxFiles KenshiCoop_* files (oldest deleted first, *.sent
// included). Called under g_cs at every segment open - the directory stays
// bounded no matter how long a session runs.
void segPrune() {
    if (!g_segDir[0] || g_segMaxFiles == 0) return;
    char pat[512];
    _snprintf(pat, sizeof(pat) - 1, "%s\\KenshiCoop_*", g_segDir);
    pat[sizeof(pat) - 1] = '\0';
    std::vector<std::pair<unsigned __int64, std::string> > files;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        unsigned __int64 t = ((unsigned __int64)fd.ftLastWriteTime.dwHighDateTime << 32)
                           | fd.ftLastWriteTime.dwLowDateTime;
        files.push_back(std::make_pair(t, std::string(fd.cFileName)));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    if (files.size() <= g_segMaxFiles) return;
    std::sort(files.begin(), files.end()); // oldest first
    unsigned int drop = (unsigned int)files.size() - g_segMaxFiles;
    for (unsigned int i = 0; i < drop; ++i) {
        char full[560];
        _snprintf(full, sizeof(full) - 1, "%s\\%s", g_segDir, files[i].second.c_str());
        full[sizeof(full) - 1] = '\0';
        DeleteFileA(full);
    }
}

// Open the next segment file (timestamp-named). Under g_cs.
void segOpenNew() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    _snprintf(g_segPath, sizeof(g_segPath) - 1,
              "%s\\KenshiCoop_%s_%04u%02u%02u_%02u%02u%02u.log",
              g_segDir, g_tag[0] ? g_tag : "LOG",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    g_segPath[sizeof(g_segPath) - 1] = '\0';
    g_segFp = std::fopen(g_segPath, "w");
    g_segStart = GetTickCount();
    segPrune();
}

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
        // Segment mirror + rotation (v48 log shipping). Rotation BEFORE the
        // write so a segment never exceeds its window by more than one line.
        if (g_segOn) {
            if (g_segFp &&
                (GetTickCount() - g_segStart) >= g_segMinutes * 60000ul) {
                std::fclose(g_segFp);
                g_segFp = 0;
                if (g_segDoneN < SEG_DONE_MAX) {
                    memcpy(g_segDone[g_segDoneN], g_segPath, sizeof(g_segPath));
                    ++g_segDoneN;
                } // ring full: segment stays on disk; the unshipped scan catches it
                segOpenNew();
            }
            if (g_segFp) {
                std::fprintf(g_segFp, "[%02lu:%02lu:%02lu.%03lu] [%s] %s: %s\n",
                             hh, mm, ss, mmm,
                             g_tag, level, msg ? msg : "");
                std::fflush(g_segFp);
            }
        }
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

void logSetTag(const char* modeTag) {
    if (!g_init || !modeTag || !modeTag[0]) return;
    EnterCriticalSection(&g_cs);
    size_t i = 0;
    for (; modeTag[i] && i < sizeof(g_tag) - 1; ++i) g_tag[i] = modeTag[i];
    g_tag[i] = '\0';
    LeaveCriticalSection(&g_cs);
}

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

void logSegmentsInit(const char* dir, unsigned int segMinutes, unsigned int maxFiles) {
    if (!g_init || !dir || !dir[0] || g_segOn) return;
    CreateDirectoryA(dir, 0); // best effort; open below reports real failure
    EnterCriticalSection(&g_cs);
    size_t i = 0;
    for (; dir[i] && i < sizeof(g_segDir) - 1; ++i) g_segDir[i] = dir[i];
    g_segDir[i]   = '\0';
    g_segMinutes  = (segMinutes > 0) ? segMinutes : 15;
    g_segMaxFiles = (maxFiles > 0) ? maxFiles : 100;
    segOpenNew();
    g_segOn = (g_segFp != 0);
    LeaveCriticalSection(&g_cs);
    logLine(g_segOn ? "[log] segment mirror armed" : "[log] segment mirror FAILED to open");
}

unsigned int logTakeFinishedSegments(char out[][512], unsigned int maxOut) {
    if (!g_init || !g_segOn || maxOut == 0) return 0;
    EnterCriticalSection(&g_cs);
    unsigned int n = (g_segDoneN < maxOut) ? g_segDoneN : maxOut;
    for (unsigned int i = 0; i < n; ++i)
        memcpy(out[i], g_segDone[i], 512);
    // Compact any remainder (ring is tiny; shipping drains faster than 15-min
    // rotations can fill it).
    unsigned int left = g_segDoneN - n;
    for (unsigned int i = 0; i < left; ++i)
        memcpy(g_segDone[i], g_segDone[n + i], 512);
    g_segDoneN = left;
    LeaveCriticalSection(&g_cs);
    return n;
}

void logCurrentSegment(char* out, unsigned int cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!g_init || !g_segOn) return;
    EnterCriticalSection(&g_cs);
    size_t i = 0;
    for (; g_segPath[i] && i < cap - 1; ++i) out[i] = g_segPath[i];
    out[i] = '\0';
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
    if (g_segFp) {
        std::fflush(g_segFp);
        std::fclose(g_segFp);
        g_segFp = 0;
    }
    LeaveCriticalSection(&g_cs);
}

} // namespace coop
