// FilePush implementation. See FilePush.h.

#define _CRT_SECURE_NO_WARNINGS 1

#include "FilePush.h"

#include "../CoopLog.h"
#include "../core/Inbound.h"
#include "../net/NetLink.h"

#include <cstdio>
#include <cstring>

namespace coop {
namespace filepush {
namespace {

// ---- Sender state (main thread only) ----------------------------------------
bool                        g_sendActive = false;
u32                         g_sendXferId = 0; // monotonic per process
u8                          g_sendPurpose = 0;
std::string                 g_sendName;
std::vector<unsigned char>  g_sendBytes;
unsigned int                g_sendOffset = 0;

// ---- Receiver state (main thread only) --------------------------------------
bool                        g_recvActive = false;
u32                         g_recvOwner  = 0;
u32                         g_recvXferId = 0;
u8                          g_recvPurpose = 0;
std::string                 g_recvName;
u32                         g_recvCrc  = 0;
u32                         g_recvSize = 0;
std::vector<unsigned char>  g_recvBuf;
unsigned int                g_recvGot  = 0;

// Completed slot (one deep).
bool                        g_doneHave = false;
u8                          g_donePurpose = 0;
std::string                 g_doneName;
std::vector<unsigned char>  g_doneBytes;
u32                         g_doneOwner = 0;

// ACK observation.
u32 g_lastAckXferId = 0;
int g_lastAckOk     = -1;

} // namespace

u32 fnv1a32(const unsigned char* p, unsigned int n) {
    u32 h = 2166136261u;
    for (unsigned int i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

bool beginSend(NetLink& net, u32 localId, u8 purpose, const char* name,
               const std::vector<unsigned char>& bytes) {
    if (g_sendActive || !name || !name[0] || bytes.empty() ||
        bytes.size() > FILE_PUSH_SIZE_MAX)
        return false;
    size_t nameLen = strlen(name);
    if (nameLen > FILE_PUSH_NAME_MAX) return false;
    g_sendActive  = true;
    g_sendPurpose = purpose;
    g_sendName    = name;
    g_sendBytes   = bytes;
    g_sendOffset  = 0;
    ++g_sendXferId;

    FilePushBeginPacket b;
    b.type    = (u8)PKT_FILE_BEGIN;
    b.ownerId = localId;
    b.xferId  = g_sendXferId;
    b.purpose = purpose;
    b.nameLen = (u16)nameLen;
    b.size    = (u32)bytes.size();
    b.crc     = fnv1a32(&bytes[0], (unsigned int)bytes.size());
    net.queueFileBegin(b, name);

    char lb[192];
    _snprintf(lb, sizeof(lb) - 1,
              "[push] BEGIN xferId=%u purpose=%u name='%s' size=%u crc=%08x",
              (unsigned)g_sendXferId, (unsigned)purpose, name,
              (unsigned)bytes.size(), (unsigned)b.crc);
    lb[sizeof(lb) - 1] = '\0';
    coop::logLine(lb);
    return true;
}

bool sending() { return g_sendActive; }

void tickSend(NetLink& net, u32 localId) {
    if (!g_sendActive) return;
    // Pacing: a DLL push has a joiner actively waiting on it (~24 KB/tick, a
    // ~1.2 MB DLL in ~2-3 s of ticks); log shipping is background freight.
    unsigned int chunksThisTick = (g_sendPurpose == FILE_PUSH_DLL) ? 24u : 6u;
    while (chunksThisTick-- > 0 && g_sendOffset < g_sendBytes.size()) {
        unsigned int n = (unsigned int)g_sendBytes.size() - g_sendOffset;
        if (n > FILE_PUSH_CHUNK_MAX) n = FILE_PUSH_CHUNK_MAX;
        FilePushChunkPacket c;
        c.type    = (u8)PKT_FILE_CHUNK;
        c.ownerId = localId;
        c.xferId  = g_sendXferId;
        c.offset  = g_sendOffset;
        c.dataLen = (u16)n;
        net.queueFileChunk(c, &g_sendBytes[g_sendOffset], n);
        g_sendOffset += n;
    }
    if (g_sendOffset >= g_sendBytes.size()) {
        FilePushDonePacket d;
        d.type    = (u8)PKT_FILE_DONE;
        d.ownerId = localId;
        d.xferId  = g_sendXferId;
        net.queueFileDone(d);
        char lb[96];
        _snprintf(lb, sizeof(lb) - 1, "[push] DONE queued xferId=%u",
                  (unsigned)g_sendXferId);
        lb[sizeof(lb) - 1] = '\0';
        coop::logLine(lb);
        g_sendActive = false;
        g_sendBytes.clear();
    }
}

void resetPeer() {
    if (g_sendActive) coop::logLine("[push] send aborted (peer left)");
    g_sendActive = false;
    g_sendBytes.clear();
    if (g_recvActive) coop::logLine("[push] receive aborted (peer left)");
    g_recvActive = false;
    g_recvBuf.clear();
}

void noteAck(const FilePushAckPacket& a) {
    g_lastAckXferId = a.xferId;
    g_lastAckOk     = a.ok ? 1 : 0;
    char lb[96];
    _snprintf(lb, sizeof(lb) - 1, "[push] ACK xferId=%u ok=%u",
              (unsigned)a.xferId, (unsigned)a.ok);
    lb[sizeof(lb) - 1] = '\0';
    coop::logLine(lb);
}

u32 lastAckXferId() { return g_lastAckXferId; }
int lastAckOk()     { return g_lastAckOk; }

void feedBegin(const InboundFileBegin& b) {
    if (b.hdr.size == 0 || b.hdr.size > FILE_PUSH_SIZE_MAX) {
        coop::logErrLine("[push] RECV-BEGIN rejected (bad size)");
        g_recvActive = false;
        return;
    }
    g_recvActive  = true;
    g_recvOwner   = b.ownerId;
    g_recvXferId  = b.hdr.xferId;
    g_recvPurpose = b.hdr.purpose;
    g_recvName    = b.name;
    g_recvCrc     = b.hdr.crc;
    g_recvSize    = b.hdr.size;
    g_recvBuf.assign(g_recvSize, 0);
    g_recvGot     = 0;
    char lb[192];
    _snprintf(lb, sizeof(lb) - 1,
              "[push] RECV-BEGIN xferId=%u purpose=%u name='%s' size=%u",
              (unsigned)g_recvXferId, (unsigned)g_recvPurpose,
              g_recvName.c_str(), (unsigned)g_recvSize);
    lb[sizeof(lb) - 1] = '\0';
    coop::logLine(lb);
}

void feedChunk(const InboundFileChunk& c) {
    if (!g_recvActive || c.hdr.xferId != g_recvXferId) return; // stale transfer
    unsigned int off = c.hdr.offset;
    unsigned int n   = (unsigned int)c.data.size();
    if (n == 0 || off > g_recvSize || n > g_recvSize - off) {
        coop::logErrLine("[push] RECV-CHUNK out of bounds; transfer dropped");
        g_recvActive = false;
        g_recvBuf.clear();
        return;
    }
    memcpy(&g_recvBuf[off], &c.data[0], n);
    g_recvGot += n;
}

void feedDone(const InboundFileDone& d, NetLink& net, u32 localId) {
    if (!g_recvActive || d.pkt.xferId != g_recvXferId) return;
    bool ok = (g_recvGot == g_recvSize) &&
              (fnv1a32(g_recvGot ? &g_recvBuf[0] : (const unsigned char*)"",
                       g_recvGot) == g_recvCrc);
    char lb[160];
    _snprintf(lb, sizeof(lb) - 1,
              "[push] RECV-DONE xferId=%u got=%u/%u crcOk=%d",
              (unsigned)g_recvXferId, g_recvGot, (unsigned)g_recvSize,
              ok ? 1 : 0);
    lb[sizeof(lb) - 1] = '\0';
    coop::logLine(lb);
    if (ok) {
        g_doneHave    = true;
        g_donePurpose = g_recvPurpose;
        g_doneName    = g_recvName;
        g_doneBytes.swap(g_recvBuf);
        g_doneOwner   = g_recvOwner;
    }
    g_recvActive = false;
    g_recvBuf.clear();

    FilePushAckPacket a;
    a.type    = (u8)PKT_FILE_ACK;
    a.ownerId = localId;
    a.xferId  = d.pkt.xferId;
    a.ok      = ok ? 1 : 0;
    net.queueFileAck(a);
}

bool takeCompleted(u8* purpose, std::string* name,
                   std::vector<unsigned char>* bytes, u32* fromOwner) {
    if (!g_doneHave) return false;
    if (purpose)   *purpose = g_donePurpose;
    if (name)      *name = g_doneName;
    if (bytes)     bytes->swap(g_doneBytes);
    if (fromOwner) *fromOwner = g_doneOwner;
    g_doneHave = false;
    g_doneBytes.clear();
    return true;
}

} // namespace filepush
} // namespace coop
