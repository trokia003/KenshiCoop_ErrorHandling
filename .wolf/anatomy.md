# Anatomy — KenshiCoop_ErrorHandling (branch: CrashHandling)

Co-op multiplayer plugin for Kenshi (RE_Kenshi/KenshiLib, C++03/VC2010 x64).

- `src/plugin/Plugin.cpp` — entry point (startPlugin), main-loop hook, session controller, coordinated save/load drive (tickCoordinatedSaveLoad — now also drives the risk checkpoint)
- `src/plugin/CoopLog.{h,cpp}` — thread-safe file logger; NEW: black-box ring (logRecentTo) + crash-safe logCrashLine
- `src/plugin/core/CrashGuard.{h,cpp}` — NEW (this branch): process-wide SetUnhandledExceptionFilter crash recorder — KenshiCoop_crash.txt report (module+offset, registers, stack scan, black box) + minidump via dbghelp; prior-session crash summary + *_prev rotation at install
- `src/plugin/core/Config.{h,cpp}` — env/coop_config.json knobs; NEW: riskSave (KENSHICOOP_RISK_SAVE, default on) + riskSaveIntervalSec (KENSHICOOP_RISK_SAVE_INTERVAL_S, 180)
- `src/plugin/sync/Replicator.h` — sync hub; NEW members: censusSquadsSeen_/censusSquadsSeeded_/riskEventMs_/riskNewSquads_ + riskCheckpointDue()
- `src/plugin/sync/ReplicatorPublish.cpp` — publishOwned + host census walk (publishNpcCensus; NEW: new-squad-cohort risk arming + riskCheckpointDue impl)
- `src/plugin/sync/ReplicatorCore.cpp` — ctor + resetSession (NEW fields initialized/cleared)
- `src/plugin/sync/ReplicatorItems.cpp` — inventory/world-item/weapon channels (dup guards; known gap: pre-existing ground GEAR untracked by both channels)
- `src/plugin/KenshiCoop.vcxproj` — NEW entries: core\CrashGuard.{cpp,h}
- `scripts/` — build (build_plugin.cmd), two-client harness (dev_cycle.ps1, regress.ps1, scenarios.psd1)
- `tools/` — user's VC2010 toolchain-repair scripts (registry/redists)
- `docs/` — BUILD_SETUP.md, API_REFERENCE.md, HOW_THE_MOD_WORKS.md (NEW explainer)
