// CrashGuard implementation. See CrashGuard.h for rationale.
//
// CRASH-SAFETY RULES (the handler runs inside a dying process, possibly with a
// corrupt heap, possibly on a thread whose stack just overflowed):
//   * no CRT heap (no new/malloc/std::string) - static buffers only;
//   * raw Win32 file IO (CreateFileA/WriteFile), never stdio, for the report;
//   * every speculative memory read (the stack scan) sits under its own SEH;
//   * the main coop log is written via logCrashLine (TryEnterCriticalSection -
//     a crashing thread that already holds the log lock must not self-deadlock);
//   * dbghelp is loaded + resolved at INSTALL time (LoadLibrary inside a crash
//     handler is unreliable);
//   * a reentry latch: a second fault while reporting just falls through to
//     the previous filter.

#define _CRT_SECURE_NO_WARNINGS 1

#include "CrashGuard.h"
#include "../CoopLog.h"

#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstring>

namespace coop {
namespace crashguard {
namespace {

typedef BOOL (WINAPI *MiniDumpWriteDumpFn)(
    HANDLE hProcess, DWORD ProcessId, HANDLE hFile, MINIDUMP_TYPE DumpType,
    PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
    PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
    PMINIDUMP_CALLBACK_INFORMATION CallbackParam);

bool                          g_installed  = false;
LPTOP_LEVEL_EXCEPTION_FILTER  g_prevFilter = 0;
MiniDumpWriteDumpFn           g_writeDump  = 0;
volatile LONG                 g_inHandler  = 0;
char                          g_tag[16]        = { 0 };
char                          g_reportPath[MAX_PATH] = { 0 };
char                          g_dumpPath[MAX_PATH]   = { 0 };

// Static scratch for the handler (never on the crashing thread's stack - it
// may be one page from exhaustion after EXCEPTION_STACK_OVERFLOW).
char                          g_line[512];
char                          g_recent[8192];

void rawWrite(HANDLE h, const char* s) {
    if (h == INVALID_HANDLE_VALUE || !s) return;
    DWORD n = 0;
    WriteFile(h, s, (DWORD)lstrlenA(s), &n, 0);
}

// Resolve 'addr' to "module.dll+0xOFFSET". Returns false when the address is
// not inside any loaded module (heap / stack / JIT garbage).
bool moduleFor(const void* addr, char* nameOut, unsigned int nameCap,
               ULONG_PTR* offOut) {
    HMODULE m = 0;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)addr, &m) || m == 0)
        return false;
    char full[MAX_PATH];
    DWORD n = GetModuleFileNameA(m, full, MAX_PATH);
    if (n == 0) return false;
    const char* base = full;
    for (const char* p = full; *p; ++p)
        if (*p == '\\' || *p == '/') base = p + 1;
    unsigned int i = 0;
    for (; base[i] && i < nameCap - 1; ++i) nameOut[i] = base[i];
    nameOut[i] = '\0';
    *offOut = (ULONG_PTR)addr - (ULONG_PTR)m;
    return true;
}

// Stack scan under its own SEH: walk the raw qwords above Rsp and report every
// value that lands inside a loaded module. Not a true unwind (no frame chain -
// x64 unwinds need the function tables and heap access we must not touch), but
// module+offset per plausible return address is exactly enough to attribute
// the crash and rough out the call path in a dead process.
void scanStack(HANDLE h, const CONTEXT* ctx) {
    __try {
#ifdef _M_X64
        const ULONG_PTR* sp = (const ULONG_PTR*)ctx->Rsp;
#else
        const ULONG_PTR* sp = (const ULONG_PTR*)ctx->Esp;
#endif
        int emitted = 0;
        for (int i = 0; i < 2048 && emitted < 48; ++i) {
            ULONG_PTR v;
            __try { v = sp[i]; }
            __except (EXCEPTION_EXECUTE_HANDLER) { break; } // ran off the stack
            if (v < 0x10000) continue;
            char mod[96]; ULONG_PTR off = 0;
            if (!moduleFor((const void*)v, mod, sizeof(mod), &off)) continue;
            _snprintf(g_line, sizeof(g_line) - 1,
                      "  stack[%4d] %s+0x%IX\n", i, mod, off);
            g_line[sizeof(g_line) - 1] = '\0';
            rawWrite(h, g_line);
            ++emitted;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        rawWrite(h, "  (stack scan faulted)\n");
    }
}

LONG WINAPI crashFilter(EXCEPTION_POINTERS* ep) {
    // Reentry latch: a fault inside the reporter (or a second thread crashing
    // concurrently) must not recurse - hand straight to the previous filter.
    if (InterlockedCompareExchange(&g_inHandler, 1, 0) != 0)
        return g_prevFilter ? g_prevFilter(ep) : EXCEPTION_CONTINUE_SEARCH;

    HANDLE h = CreateFileA(g_reportPath, GENERIC_WRITE, FILE_SHARE_READ, 0,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);

    DWORD code = 0; const void* at = 0;
    if (ep && ep->ExceptionRecord) {
        code = ep->ExceptionRecord->ExceptionCode;
        at   = ep->ExceptionRecord->ExceptionAddress;
    }

    char atMod[96] = "?"; ULONG_PTR atOff = 0;
    bool haveMod = at && moduleFor(at, atMod, sizeof(atMod), &atOff);

    if (h != INVALID_HANDLE_VALUE) {
        unsigned long ms = wallClockMs();
        _snprintf(g_line, sizeof(g_line) - 1,
                  "KenshiCoop crash report [%s]\n"
                  "time=%02lu:%02lu:%02lu.%03lu thread=%lu\n"
                  "exception=0x%08lX address=%p",
                  g_tag,
                  (ms / 3600000ul) % 24ul, (ms / 60000ul) % 60ul,
                  (ms / 1000ul) % 60ul, ms % 1000ul,
                  GetCurrentThreadId(), (unsigned long)code, at);
        g_line[sizeof(g_line) - 1] = '\0';
        rawWrite(h, g_line);
        if (haveMod) {
            _snprintf(g_line, sizeof(g_line) - 1, " = %s+0x%IX", atMod, atOff);
            g_line[sizeof(g_line) - 1] = '\0';
            rawWrite(h, g_line);
        }
        rawWrite(h, "\n");
        // Access violations carry read/write + target address in the params.
        if (ep && ep->ExceptionRecord &&
            code == EXCEPTION_ACCESS_VIOLATION &&
            ep->ExceptionRecord->NumberParameters >= 2) {
            _snprintf(g_line, sizeof(g_line) - 1,
                      "access-violation: %s address 0x%IX\n",
                      ep->ExceptionRecord->ExceptionInformation[0] == 0 ? "READ"
                      : ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "WRITE"
                      : "EXECUTE",
                      (ULONG_PTR)ep->ExceptionRecord->ExceptionInformation[1]);
            g_line[sizeof(g_line) - 1] = '\0';
            rawWrite(h, g_line);
        }
#ifdef _M_X64
        if (ep && ep->ContextRecord) {
            const CONTEXT* c = ep->ContextRecord;
            _snprintf(g_line, sizeof(g_line) - 1,
                      "rip=%016I64X rsp=%016I64X rbp=%016I64X\n"
                      "rax=%016I64X rbx=%016I64X rcx=%016I64X rdx=%016I64X\n"
                      "rsi=%016I64X rdi=%016I64X r8=%016I64X r9=%016I64X\n"
                      "r10=%016I64X r11=%016I64X r12=%016I64X r13=%016I64X\n"
                      "r14=%016I64X r15=%016I64X\n",
                      c->Rip, c->Rsp, c->Rbp, c->Rax, c->Rbx, c->Rcx, c->Rdx,
                      c->Rsi, c->Rdi, c->R8, c->R9, c->R10, c->R11, c->R12,
                      c->R13, c->R14, c->R15);
            g_line[sizeof(g_line) - 1] = '\0';
            rawWrite(h, g_line);
        }
#endif
        rawWrite(h, "---- stack (module-resolved scan) ----\n");
        if (ep && ep->ContextRecord) scanStack(h, ep->ContextRecord);
        rawWrite(h, "---- recent events (black box) ----\n");
        unsigned int n = logRecentTo(g_recent, sizeof(g_recent));
        if (n) rawWrite(h, g_recent);
    }

    // Minidump AFTER the text report (the report must survive even if the
    // dump write itself faults - the reentry latch turns that into a chain).
    bool dumped = false;
    if (g_writeDump) {
        HANDLE hd = CreateFileA(g_dumpPath, GENERIC_WRITE, 0, 0,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (hd != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei;
            mei.ThreadId          = GetCurrentThreadId();
            mei.ExceptionPointers = ep;
            mei.ClientPointers    = FALSE;
            dumped = g_writeDump(GetCurrentProcess(), GetCurrentProcessId(), hd,
                                 (MINIDUMP_TYPE)(MiniDumpNormal |
                                                 MiniDumpWithDataSegs |
                                                 MiniDumpWithIndirectlyReferencedMemory),
                                 ep ? &mei : 0, 0, 0) != FALSE;
            CloseHandle(hd);
        }
    }
    if (h != INVALID_HANDLE_VALUE) {
        rawWrite(h, dumped ? "minidump: written\n" : "minidump: FAILED\n");
        CloseHandle(h);
    }

    // Best-effort FATAL line into the session log (try-lock; see CoopLog).
    _snprintf(g_line, sizeof(g_line) - 1,
              "[crash] FATAL exception=0x%08lX at %s+0x%IX (report='%s' dump=%s)",
              (unsigned long)code, haveMod ? atMod : "?", atOff,
              g_reportPath, dumped ? "yes" : "no");
    g_line[sizeof(g_line) - 1] = '\0';
    logCrashLine(g_line);

    return g_prevFilter ? g_prevFilter(ep) : EXCEPTION_CONTINUE_SEARCH;
}

// "<log dir>\\<name>" into out. logPath may be a bare filename (dir = "").
void siblingPath(const char* logPath, const char* name, char* out, unsigned int cap) {
    unsigned int dirLen = 0;
    if (logPath) {
        for (unsigned int i = 0; logPath[i]; ++i)
            if (logPath[i] == '\\' || logPath[i] == '/') dirLen = i + 1;
    }
    unsigned int o = 0;
    for (unsigned int i = 0; i < dirLen && o < cap - 1; ++i) out[o++] = logPath[i];
    for (unsigned int i = 0; name[i] && o < cap - 1; ++i)    out[o++] = name[i];
    out[o] = '\0';
}

// Prior-session artifact: summarize into the fresh log, rotate to *_prev.
void reportPriorCrash(const char* logPath) {
    if (GetFileAttributesA(g_reportPath) == INVALID_FILE_ATTRIBUTES) return;

    char head[300] = { 0 };
    HANDLE h = CreateFileA(g_reportPath, GENERIC_READ, FILE_SHARE_READ, 0,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD n = 0;
        ReadFile(h, head, sizeof(head) - 1, &n, 0);
        head[n] = '\0';
        CloseHandle(h);
        for (unsigned int i = 0; head[i]; ++i)
            if (head[i] == '\n' || head[i] == '\r') head[i] = ' ';
    }

    char prev[MAX_PATH];
    siblingPath(logPath, "KenshiCoop_crash_prev.txt", prev, sizeof(prev));
    MoveFileExA(g_reportPath, prev, MOVEFILE_REPLACE_EXISTING);
    char prevDmp[MAX_PATH];
    siblingPath(logPath, "KenshiCoop_crash_prev.dmp", prevDmp, sizeof(prevDmp));
    if (GetFileAttributesA(g_dumpPath) != INVALID_FILE_ATTRIBUTES)
        MoveFileExA(g_dumpPath, prevDmp, MOVEFILE_REPLACE_EXISTING);

    char b[512];
    _snprintf(b, sizeof(b) - 1,
              "[crash] PREVIOUS session ended in a crash: %s (rotated to *_prev)",
              head[0] ? head : "(report unreadable)");
    b[sizeof(b) - 1] = '\0';
    logLine(b);
}

} // namespace

void install(const char* logPath, const char* modeTag) {
    if (g_installed) return;
    g_installed = true;

    if (modeTag) {
        unsigned int i = 0;
        for (; modeTag[i] && i < sizeof(g_tag) - 1; ++i) g_tag[i] = modeTag[i];
        g_tag[i] = '\0';
    }
    siblingPath(logPath, "KenshiCoop_crash.txt", g_reportPath, sizeof(g_reportPath));
    siblingPath(logPath, "KenshiCoop_crash.dmp", g_dumpPath,  sizeof(g_dumpPath));

    reportPriorCrash(logPath);

    // Resolve dbghelp NOW; a crash handler must not LoadLibrary.
    HMODULE dbg = LoadLibraryA("dbghelp.dll");
    if (dbg) g_writeDump = (MiniDumpWriteDumpFn)GetProcAddress(dbg, "MiniDumpWriteDump");

    g_prevFilter = SetUnhandledExceptionFilter(crashFilter);

    char b[384];
    _snprintf(b, sizeof(b) - 1,
              "[crash] guard installed report='%s' dump='%s' minidump=%d chained=%d",
              g_reportPath, g_dumpPath, g_writeDump ? 1 : 0, g_prevFilter ? 1 : 0);
    b[sizeof(b) - 1] = '\0';
    logLine(b);
}

} // namespace crashguard
} // namespace coop
