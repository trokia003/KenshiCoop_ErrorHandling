# Anatomy — KenshiCoop_ErrorHandling (branch: CrashHandling)

Co-op multiplayer plugin for Kenshi (RE_Kenshi/KenshiLib, C++03/VC2010 x64).

- `src/plugin/Plugin.cpp` — entry point (startPlugin), main-loop hook, session controller, coordinated save/load drive (tickCoordinatedSaveLoad — drives the risk checkpoint AND the resync state machine: F2 Resync button → save 'coop_resync' → join ACK/XFER-SKIP → coordinated reload)
- `src/plugin/CoopLog.{h,cpp}` — thread-safe file logger; black-box ring (logRecentTo) + crash-safe logCrashLine; NEW v48: MP log segment mirror (logSegmentsInit — 15-min rotating files under KenshiCoopLogs\, 100-file cap, main log untouched for the oracles; logTakeFinishedSegments/logCurrentSegment)
- `src/plugin/core/SelfUpdate.{h,cpp}` — NEW v48: own-DLL identity (path/FNV-1a-32 fingerprint) + rename-swap installer (applyImage: .new → live→.old → .new→live; cleanupOld at startup)
- `src/plugin/sync/FilePush.{h,cpp}` — NEW v48: generic in-band file channel (PKT_FILE_BEGIN/CHUNK/DONE/ACK over CH_BULK, purpose-tagged DLL|LOG, paced sender + CRC-verified in-memory receiver); WIRE FORMAT FROZEN (cross-version bootstrap subset)
- `src/plugin/core/CrashGuard.{h,cpp}` — NEW (this branch): process-wide SetUnhandledExceptionFilter crash recorder — KenshiCoop_crash.txt report (module+offset, registers, stack scan, black box) + minidump via dbghelp; prior-session crash summary + *_prev rotation at install
- `src/plugin/core/Config.{h,cpp}` — env/coop_config.json knobs; NEW: riskSave (KENSHICOOP_RISK_SAVE, default on) + riskSaveIntervalSec (KENSHICOOP_RISK_SAVE_INTERVAL_S, 180) + spawnVeto (KENSHICOOP_SPAWN_VETO, default on — join world-spawn veto)
- `src/plugin/game/ZoneQuery.cpp` — zone-loaded query (quarantined ZoneManager.h TU); NEW: join world-spawn veto — spawnChecks_hook detour on ZoneManager::spawnChecksUpdateThreaded (installSpawnVetoHook/setSpawnVeto/spawnVetoTicks) + town-flavor veto — barFlies_hook detour on Town::spawnTheBarFlies (installTownVetoHook/setTownVeto/townVetoCalls); both armed per tick from Plugin::tickReplicateApply on live join sessions only (knobs KENSHICOOP_SPAWN_VETO / KENSHICOOP_TOWN_VETO)
- `src/plugin/sync/Replicator.h` — sync hub; NEW members: censusSquadsSeen_/censusSquadsSeeded_/riskEventMs_/riskNewSquads_ + riskCheckpointDue()
- `src/plugin/sync/ReplicatorPublish.cpp` — publishOwned + host census walk (publishNpcCensus; NEW: new-squad-cohort risk arming + riskCheckpointDue impl)
- `src/plugin/sync/ReplicatorCore.cpp` — ctor + resetSession (NEW fields initialized/cleared)
- `src/plugin/sync/ReplicatorItems.cpp` — inventory/world-item/weapon channels (dup guards; known gap: pre-existing ground GEAR untracked by both channels)
- `src/plugin/KenshiCoop.vcxproj` — NEW entries: core\CrashGuard.{cpp,h}
- `scripts/` — build (build_plugin.cmd), two-client harness (dev_cycle.ps1, regress.ps1, scenarios.psd1)
- `tools/` — user's VC2010 toolchain-repair scripts (registry/redists)
- `docs/` — BUILD_SETUP.md, API_REFERENCE.md, HOW_THE_MOD_WORKS.md (NEW explainer)
