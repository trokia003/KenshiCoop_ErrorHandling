// ZoneQuery.cpp - the zone-loaded query (Phase 1 spawn parity), quarantined in
// its own TU because kenshi/ZoneManager.h redefines ParticlePool (also defined
// by kenshi/CombatClass.h, which EngineInternal.cpp needs) - the two vendored headers
// cannot share a translation unit.
//
// Why the query exists: within a locally LOADED world block, every baked
// (shared-save) NPC resolves by hand. So an unresolvable census hand whose
// host-reported position sits in a loaded block is a genuine host RUNTIME
// spawn - safe to proxy-mint at any distance without duplicate risk. This
// generalizes the fixed spawnMintRadius_ gate (a cheap stand-in for "the
// block here is certainly loaded") to the engine's own answer.

// ZoneManager.h pulls boost/thread headers (shared_mutex members), whose
// auto-link pragma demands libboost_thread-*.lib. We only form member-function
// pointers on the class (never instantiate it), so no boost code is generated -
// suppress the auto-link instead of shipping the library.
#define BOOST_ALL_NO_LIB

#include <windows.h>

#include <core/Functions.h>     // KenshiLib::GetRealAddress / AddHook
#include <kenshi/GameWorld.h>   // GameWorld::zoneMgr
#include <kenshi/ZoneManager.h> // ZoneManager::_NV_isZoneLoadedT / _NV_isZoneBeingLoadedT
#include <kenshi/Town.h>        // Town::spawnTheBarFlies (town-flavor veto)

namespace coop {
namespace engine {

namespace {
// this=RCX, const Ogre::Vector3& = pointer in RDX.
typedef bool (__fastcall* ZoneLoadedFn)(ZoneManager* self, const Ogre::Vector3* pos);
ZoneLoadedFn g_zoneLoadedFn      = 0;
ZoneLoadedFn g_zoneBeingLoadedFn = 0;

// Join world-spawn veto (ghost prevention at the source). While a join session
// is live the host census is the sole NPC-existence authority: any squad this
// engine generates on its own (ambient wildlife / roaming patrols - the
// ZoneManager spawn-checks ticker) is a local-only ghost that the authority
// pass must then chase down and hide. Skipping the ticker prevents the whole
// class before it materializes. State is section-private (the detour target
// needs the quarantined ZoneManager type); flag is written from the main tick
// and read on the zone worker thread, so the counter is interlocked.
typedef void (__fastcall* SpawnChecksFn)(ZoneManager* self, int island);
SpawnChecksFn         g_spawnChecksOrig = 0;
volatile bool         g_spawnVetoOn     = false;
volatile LONG         g_spawnVetoTicks  = 0;

void __fastcall spawnChecks_hook(ZoneManager* self, int island) {
    if (g_spawnVetoOn) {
        InterlockedIncrement(&g_spawnVetoTicks);
        return; // no local world spawns while the host owns NPC existence
    }
    if (g_spawnChecksOrig) g_spawnChecksOrig(self, island);
}

// Town-flavor veto (same regime, separate knob/counter): bar patrons are
// generated per-visit by Town::spawnTheBarFlies, so with both engines rolling
// their own the join's bars fill with census-absent ghosts standing beside the
// host's proxies. The shared player faction keeps the join squad's zones
// simulated HOST-side too, so the host's own barflies cover the join's bars
// through the normal census/spawn-info proxy pipeline - skipping the local
// roll loses nothing. Functional town population (shopkeepers, guards -
// populateBuilding/createCharacterForBuilding) is deliberately NOT vetoed.
typedef void (__fastcall* BarFliesFn)(Town* self);
BarFliesFn            g_barFliesOrig  = 0;
volatile bool         g_townVetoOn    = false;
volatile LONG         g_townVetoCalls = 0;

void __fastcall barFlies_hook(Town* self) {
    if (g_townVetoOn) {
        InterlockedIncrement(&g_townVetoCalls);
        return; // host-authored barflies arrive as proxies instead
    }
    if (g_barFliesOrig) g_barFliesOrig(self);
}
} // namespace

bool installSpawnVetoHook() {
    intptr_t addr = KenshiLib::GetRealAddress(&ZoneManager::spawnChecksUpdateThreaded);
    if (!addr) return false;
    return KenshiLib::AddHook(addr, (void*)&spawnChecks_hook,
                              (void**)&g_spawnChecksOrig) == KenshiLib::SUCCESS;
}

void setSpawnVeto(bool on)       { g_spawnVetoOn = on; }
unsigned long spawnVetoTicks()   { return (unsigned long)g_spawnVetoTicks; }

bool installTownVetoHook() {
    intptr_t addr = KenshiLib::GetRealAddress(&Town::spawnTheBarFlies);
    if (!addr) return false;
    return KenshiLib::AddHook(addr, (void*)&barFlies_hook,
                              (void**)&g_barFliesOrig) == KenshiLib::SUCCESS;
}

void setTownVeto(bool on)        { g_townVetoOn = on; }
unsigned long townVetoCalls()    { return (unsigned long)g_townVetoCalls; }

// Called from engine::resolve(). Resolved via the _NV_ non-virtual aliases
// (concrete RVAs; the virtual names resolve to vtable thunks).
void resolveZoneQuery() {
    g_zoneLoadedFn = (ZoneLoadedFn)KenshiLib::GetRealAddress(
        &ZoneManager::_NV_isZoneLoadedT);
    g_zoneBeingLoadedFn = (ZoneLoadedFn)KenshiLib::GetRealAddress(
        &ZoneManager::_NV_isZoneBeingLoadedT);
}

bool isZoneLoadedAt(GameWorld* gw, float x, float y, float z) {
    if (!gw || !g_zoneLoadedFn) return false;
    __try {
        ZoneManager* zm = gw->zoneMgr;
        if (!zm) return false;
        Ogre::Vector3 p(x, y, z);
        if (!g_zoneLoadedFn(zm, &p)) return false;
        // A block MID-LOAD hasn't materialized its baked NPCs yet - treating it
        // as loaded would far-mint a duplicate of a body about to appear.
        if (g_zoneBeingLoadedFn && g_zoneBeingLoadedFn(zm, &p)) return false;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace engine
} // namespace coop
