// SelfUpdate implementation. See SelfUpdate.h.

#define _CRT_SECURE_NO_WARNINGS 1

#include "SelfUpdate.h"
#include "../CoopLog.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

namespace coop {
namespace selfupdate {
namespace {

// Same FNV-1a-32 the save-transfer CRC table uses; computed independently on
// both ends of the push, so only internal consistency matters.
unsigned int fnv1a32(const unsigned char* p, size_t n) {
    unsigned int h = 2166136261u;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

bool         g_infoCached = false;
unsigned int g_selfCrc    = 0;
unsigned int g_selfSize   = 0;

bool readFileBytes(const char* path, std::vector<unsigned char>& out) {
    out.clear();
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    bool ok = false;
    if (sz > 0) {
        out.resize((size_t)sz);
        ok = (std::fread(&out[0], 1, (size_t)sz, f) == (size_t)sz);
        if (!ok) out.clear();
    }
    std::fclose(f);
    return ok;
}

} // namespace

bool selfPath(char* out, unsigned int cap) {
    if (!out || cap == 0) return false;
    out[0] = '\0';
    HMODULE h = 0;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)&selfPath, &h) ||
        !h)
        return false;
    DWORD n = GetModuleFileNameA(h, out, cap);
    return n > 0 && n < cap;
}

bool selfInfo(unsigned int* crc, unsigned int* size) {
    if (!g_infoCached) {
        char path[MAX_PATH];
        std::vector<unsigned char> bytes;
        if (!selfPath(path, sizeof(path)) || !readFileBytes(path, bytes))
            return false;
        g_selfCrc  = fnv1a32(&bytes[0], bytes.size());
        g_selfSize = (unsigned int)bytes.size();
        g_infoCached = true;
    }
    if (crc)  *crc  = g_selfCrc;
    if (size) *size = g_selfSize;
    return true;
}

bool readSelfBytes(std::vector<unsigned char>& out) {
    char path[MAX_PATH];
    if (!selfPath(path, sizeof(path))) return false;
    return readFileBytes(path, out);
}

bool applyImage(const std::vector<unsigned char>& bytes) {
    if (bytes.empty()) return false;
    char self[MAX_PATH];
    if (!selfPath(self, sizeof(self))) return false;
    char pNew[MAX_PATH + 8], pOld[MAX_PATH + 8];
    _snprintf(pNew, sizeof(pNew) - 1, "%s.new", self);
    pNew[sizeof(pNew) - 1] = '\0';
    _snprintf(pOld, sizeof(pOld) - 1, "%s.old", self);
    pOld[sizeof(pOld) - 1] = '\0';

    // Stage the full image beside the live DLL first - the swap below is two
    // renames, so a partial write can never end up under the live name.
    FILE* f = std::fopen(pNew, "wb");
    if (!f) {
        coop::logErrLine("[update] cannot open .new for write");
        return false;
    }
    size_t wrote = std::fwrite(&bytes[0], 1, bytes.size(), f);
    std::fclose(f);
    if (wrote != bytes.size()) {
        coop::logErrLine("[update] short write staging .new");
        DeleteFileA(pNew);
        return false;
    }

    // Rename dance: the loaded DLL's file is write-locked but RENAMABLE.
    DeleteFileA(pOld); // a leftover .old would fail the first rename
    if (!MoveFileExA(self, pOld, MOVEFILE_REPLACE_EXISTING)) {
        coop::logErrLine("[update] rename live -> .old FAILED");
        DeleteFileA(pNew);
        return false;
    }
    if (!MoveFileExA(pNew, self, 0)) {
        // Put the original back so the install stays bootable.
        MoveFileExA(pOld, self, 0);
        coop::logErrLine("[update] rename .new -> live FAILED (original restored)");
        return false;
    }
    return true;
}

void cleanupOld() {
    char self[MAX_PATH];
    if (!selfPath(self, sizeof(self))) return;
    char pOld[MAX_PATH + 8];
    _snprintf(pOld, sizeof(pOld) - 1, "%s.old", self);
    pOld[sizeof(pOld) - 1] = '\0';
    DeleteFileA(pOld); // best effort; fails harmlessly if absent
}

bool relaunchGame() {
    char exe[MAX_PATH];
    if (!GetModuleFileNameA(0, exe, sizeof(exe))) return false;
    // CreateProcess may scribble on the command-line buffer - pass a copy.
    static char cmd[2048];
    const char* orig = GetCommandLineA();
    strncpy(cmd, orig ? orig : "", sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessA(exe, cmd, 0, 0, FALSE, 0, 0, 0, &si, &pi))
        return false;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

} // namespace selfupdate
} // namespace coop
