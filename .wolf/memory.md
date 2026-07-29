# Session memory

## 2026-07-25 — CrashHandling branch: crash recorder + checkpoint-on-risk
- Implemented CrashGuard (core/CrashGuard.{h,cpp}): process-wide unhandled-exception filter → KenshiCoop_crash.txt (exception, module+offset, registers, stack scan, CoopLog black-box ring) + KenshiCoop_crash.dmp minidump; chains prior filter; rotates artifacts to *_prev and logs a "[crash] PREVIOUS session ended in a crash" summary at next launch. Wired into startPlugin right after logInit; added to vcxproj.
- Extended CoopLog with a 64-line lock-free black-box ring (logRecentTo) and try-lock logCrashLine.
- Implemented checkpoint-on-risk: host census walk (ReplicatorPublish::publishNpcCensus) arms a risk edge when a NEW squad cohort (hand t,c,cs triple) enters the census after the first seeded walk; Plugin::tickCoordinatedSaveLoad consumes it after an 8 s settle and issues coordinated save 'coop_risk' (normal protocol-31 flow), throttled by KENSHICOOP_RISK_SAVE_INTERVAL_S (180 s default), gated on host+peer+world-live+no-save-in-flight. Config: KENSHICOOP_RISK_SAVE default ON; 'riskSave' in describeConfig roster.
- Motivation: 2026-07-22 vanilla host crash — process died ~18 s after a 12-member NPC squad entered the census while the join looped spawn-info requests; no artifacts existed to attribute it (that gap is what CrashGuard closes).
- Syntax-checked CrashGuard.cpp + CoopLog.cpp with VS2022 cl /W3 (clean). FULL v100 build NOT possible in this copy yet: VC2010 toolset + SDK 7.1 not installed at script paths, third_party/KenshiLib_deps + enet clone missing after the user's file reorganization.
- No commits made (standing rule: no git/GitHub actions without express user approval).

## 2026-07-26 - v100 build UNBLOCKED and GREEN
- User installed SDK 7.1 (compilers via KB2519277 workaround - checkbox was greyed by .NET 4 detection) + reinstalled redists. cl.exe 16.00.40219.01 x64 verified.
- Fixed 4 build blockers (see buglog.json): vswhere for/f bug in build_plugin.cmd, unextracted boost.zip, deps pinned to e75769b (0.4.0 moved CombatClass.h), 2 VC10 header fixes captured in third_party/kenshilib_patches/.
- RESULT: src/plugin/x64/Harness/KenshiCoop.dll (1.16 MB) builds clean - CrashGuard + checkpoint-on-risk included. Protocol still v45 (no wire change; risk save rides the normal protocol-31 save flow).
- Not yet done: deploy to A:\SteamLibrary Kenshi install / two-install harness smoke run (BUILD_SETUP.md Parts D-E doc is missing from repo - docs/BUILD_SETUP.md is a stub pointing at nonexistent resources/).

## 2026-07-28 - first live sessions on the new build; Drew-side crash analyzed
- Connect-stall fixed (peer-settle gate) and verified live: connect, save push, risk checkpoint all worked.
- Drew (join, stock v0.46 DLL, no CrashGuard) crashed at 13:54 while receiving/committing the mid-session autosave transfer (76 files/6.9 MB, pause edge mid-transfer); survived the earlier 30-file transfer. Host never crashed. Suspect: join-side protocol-31 receive/commit path under load.
- Fixed risk-gate bug: g_savePending never clears after normal transfers -> gate on watching()/sending() instead. Rebuilt; NOT deployed (user retracted auto-deploy).
- Upstream quirk observed: reconnect assigns a new peer id (id=2) and logs '3+ players unsupported' ERROR - id counter never resets.
- Evidence wanted from Drew: his KenshiCoop_join.log (overwritten on next launch!), Windows Event Viewer Application/Event 1000 for kenshi_x64.exe, or best: run the CrashGuard build (protocol-compatible v45).

## 2026-07-28 - four live-session sync issues triaged (host log evidence)
1. Drew teleporting: ~20k snapSq hard-snaps, extrap 50k+, jit spikes 71ms, periodic starve=1 - join's entity stream gappy (his uplink and/or framerate). Tunable via interp knobs; not a discrete code bug.
2. Drew sees NPCs/items host doesn't: census publishes fine (n=17 mid=17); join-side wide culling DISABLES while census is stale (his starving link) -> ghosts; no join-side ghost-ITEM culling exists at all; beyond 2000u divergence is by design.
3. Buildings: [build] PLACE detour NEVER fired all session (only install lines; RESYNC place=0 always) - capture broken for the user's placement path; construction-site material inventories not covered by store/prod sync either. Task #1.
4. Money: [money] SEND rank=0 cats=0 all session despite real purchases - protocol-22 reads the rank-0 platoon's Ownerships::money which is 0 in this save; real cats live elsewhere. Task #2. Matches upstream #27.

## 2026-07-28 (later) - Drew's logs analyzed; building + money fixes implemented
- Drew runs the new DLL (user passed it along). His crash report: EXECUTE AV in Plugin_ParticleUniverse_x64.dll (stock Kenshi particle plugin, render worker thread) - NOT KenshiCoop, NOT the save transfer. His log (78 MB - join-side SCENARIO PROXY diagnostic spam in Harness builds is heavy) confirms: money cats=0 his side too, ghost-culling works except in 12 census-stale windows, census parks 500+.
- Building fix: loud hook + fallback site census (publishBuilds 1a) + apply-side shared-site dup guard. BuildRead gained yaw; engine::queueBuildEdgeRec added.
- Money fix: Character::getMoney read + verify-ladder write (Ownerships::setMoney -> verify -> takeMoney delta). Wallet-probe log will name the real storage next session. NOTE: first attempt targeted Platoon::getMoney/setMoney - those members belong to class Ownerships in Platoon.h (misread); reverted.
- Config: interp/snap knobs (interpMinDelayMs/interpMaxDelayMs/interpMaxExtrapMs/interpStaleMs/interpSnapDist/catchupK/snapDist/snapSeconds) now readable from coop_config.json (env still wins) - for Drew-side jitter tuning without env vars.
- Build green, contract tests 29/29. NOT deployed (user decides; both machines should get the same build - protocol still v45).

## 2026-07-28 (evening) - ground-gear ghost fix implemented
- Apply-side spatial fallback in applyWeaponPickups: ref=0/0 + empty tracked queue -> engine::findGroundItemBySidNear (new, EngineInventory.cpp: ITEM query near picker, FREE items only, sid strcmp, nearest) -> addItemPtrToInventory. ghostFb flag in the PICKUP-APPLY log line.
- Closes the original 2026-07-21 dup report. Both machines need this build for both pickup directions. Build green, contract 29/29. NOT deployed.

## 2026-07-28 (night) - PROTOCOL 46: six-fix batch, both configs green
1. DELTA SAVE TRANSFER: sender remembers per-file CRCs of last ACKed transfer per save name (SaveXfer); sends only changed files (SaveBeginPacket.delta); receiver MERGE-commits onto its copy; deletions/ACK-fail/disconnect force full send (savexfer::resetPeerState on peer-leave in Plugin). XFER-SKIP when byte-identical.
2. SHARED-WALLET MONEY v46: host streams absolute (tabRank=0), join sends signed deltas (tabRank=1) vs moneyExpected_; host folds deltas, join adopts absolutes. Single-writer, commuting.
3. NON-GEAR GHOSTS: PKT_WORLD_TAKE (43) - baseline track death near a player broadcasts sid+pos; peer destroys nearest FREE same-sid item within 24u (proxy-excluded), retiring its own baseline track first (no echo). Zone-unload deaths (no player within 60u) stay silent (wipe guard). New engine fns findGroundItemAt/destroyGroundItemPtr.
4. BAKED-SITE PROGRESS: shared save-native construction sites stream BUILD_STATE by save-stable hand from the census walk; apply side MAX-merges (no owner, no seq needed); completion latch when a site leaves the incomplete census as complete.
5. ID HYGIENE: host resets nextId to 1 when the last peer disconnects (no more '3+ players' error on reconnect).
6. DUAL BUILDS: scripts/build_both.cmd stages builds/release/KenshiCoop (924KB play build) + builds/debug/KenshiCoop (1.19MB Harness). SCENARIO PROXY telemetry now gated to scenario runs / KENSHICOOP_PROXY_DUMP=1 (was 78MB/evening on Drew's machine).
- PROTOCOL_VERSION 45 -> 46 (money semantics + WORLD_TAKE + SaveBegin.delta). BOTH players must update. Contract tests 29/29. NOT deployed.
- Gotcha fixed: WorldQ members need ctor-list registration (wt_(worldReset_)).
