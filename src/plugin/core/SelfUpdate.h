// SelfUpdate - the join side of the v48 DLL auto-update: identify THIS loaded
// KenshiCoop.dll on disk (path + FNV-1a-32 fingerprint) and swap a verified
// replacement image into place using the rename dance Windows allows on a
// loaded module (rename the locked file, write the new one at its name; the
// swap takes effect at the next Kenshi launch).
//
// Plain Win32 + C++03; no engine or wire dependencies (Plugin.cpp wires it to
// the FilePush channel).

#ifndef KENSHICOOP_SELFUPDATE_H
#define KENSHICOOP_SELFUPDATE_H

#include <vector>

namespace coop {
namespace selfupdate {

// Full path of the loaded KenshiCoop.dll. False if it cannot be resolved.
bool selfPath(char* out, unsigned int cap);

// FNV-1a-32 + byte size of the DLL file as loaded (read once, cached). The
// fingerprint both sides exchange in PKT_BUILD_INFO.
bool selfInfo(unsigned int* crc, unsigned int* size);

// Read the DLL file's bytes (for the HOST push side). False on IO failure.
bool readSelfBytes(std::vector<unsigned char>& out);

// Stage 'bytes' as <self>.new, then rename <self> -> <self>.old (allowed while
// loaded) and <self>.new -> <self>. On any failure the original name is
// restored best-effort and false is returned. The RUNNING code keeps executing
// the old image either way - the new build loads at the next launch.
bool applyImage(const std::vector<unsigned char>& bytes);

// Best-effort delete of a leftover <self>.old from a previous update. Call
// once at plugin startup. (The .old cannot be deleted in the updated session
// itself - it IS the running module's file, image-locked until exit.)
void cleanupOld();

// Relaunch the game: spawn a fresh Kenshi process with THIS process's exact
// command line (RE_Kenshi/Steam args preserved), returning true once the
// child is running. The caller then terminates this process (multiple Kenshi
// instances coexist fine - the two-install harness relies on it - so the
// brief overlap is harmless). Used by the DLL auto-update's menu restart.
bool relaunchGame();

} // namespace selfupdate
} // namespace coop

#endif // KENSHICOOP_SELFUPDATE_H
