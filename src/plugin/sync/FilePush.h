// FilePush - the v48 generic in-band file channel: one purpose-tagged
// whole-file delivery over CH_BULK, used by the DLL auto-update (host -> join)
// and MP log shipping (join -> host).
//
// Sender: beginSend snapshots the bytes; tickSend paces FILE_BEGIN/CHUNK/DONE
// onto NetLink (one transfer in flight; callers queue their own backlog).
// Receiver: feed* assembles chunks in memory, CRC-verifies at DONE and answers
// with a FILE_ACK; the completed payload waits in a single slot for
// takeCompleted. Main-thread only (all entry points), like SaveXfer.

#ifndef KENSHICOOP_FILEPUSH_H
#define KENSHICOOP_FILEPUSH_H

#include <string>
#include <vector>

#include "../../netproto/Wire.h"

namespace coop {

class NetLink;
struct InboundFileBegin;
struct InboundFileChunk;
struct InboundFileDone;

namespace filepush {

// FNV-1a-32 over a byte range (the CRC both ends compute independently).
u32 fnv1a32(const unsigned char* p, unsigned int n);

// ---- Sender ------------------------------------------------------------------

// Start pushing 'bytes' as 'name' (file NAME only, <= FILE_PUSH_NAME_MAX).
// False when a push is already active, bytes is empty/oversized, or the name
// is invalid. Logs "[push] BEGIN ...".
bool beginSend(NetLink& net, u32 localId, u8 purpose, const char* name,
               const std::vector<unsigned char>& bytes);
bool sending();
// Queue the next paced chunk batch (call every main-loop tick). DLL pushes
// drain faster than log shipping (a joiner is WAITING on the update).
void tickSend(NetLink& net, u32 localId);
// Abort the active send + pending receive (peer left).
void resetPeer();

// Sender-side ACK observation (Plugin feeds drained FILE_ACKs here).
void noteAck(const FilePushAckPacket& a);
u32  lastAckXferId(); // 0 = none yet
int  lastAckOk();     // valid when lastAckXferId != 0

// ---- Receiver ----------------------------------------------------------------

void feedBegin(const InboundFileBegin& b);
void feedChunk(const InboundFileChunk& c);
// Verify + ACK. A passing payload lands in the completed slot (one deep - a
// sender never overlaps transfers, and Plugin drains every tick).
void feedDone(const InboundFileDone& d, NetLink& net, u32 localId);
// Take the completed payload if one is waiting. Returns false when none.
bool takeCompleted(u8* purpose, std::string* name,
                   std::vector<unsigned char>* bytes, u32* fromOwner);

} // namespace filepush
} // namespace coop

#endif // KENSHICOOP_FILEPUSH_H
