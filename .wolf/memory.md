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

## 2026-07-29 - friend-code persistence
- coop::savePeerToFile (Config.cpp): line-level writeback of 'steamPeer' into coop_config.json (comments/keys preserved; inserts or creates file when absent); called from EngineUi onPasteIdBtn on a VALIDATED paste only. Logs '[coop-ui] friend code saved to coop_config.json'. No wire change (still v46). Both configs rebuilt + staged in builds/, contract 29/29. NOT deployed.

## 2026-07-29 - native-twin ADOPTION (walk-near item dup fix)
- Root cause found by reading applyWorldItems: NO dedupe at mint - every new (owner,netId) row blindly spawnWorldItemProxy'd. Save-native items discovered MID-SESSION (outside the load-time 60u baseline) are streamed by BOTH sides -> each minted a proxy on top of its own native = 2 items per screen just by walking near loot.
- Fix: at mint, findGroundItemAt same-sid FREE twin within 3u (proxies excluded) -> ADOPT it as the proxy body instead of spawning; hand over authorship (erase own worldTrack_ for that object; if we ever streamed it, queue its netId in retractNetIds_ -> next publish emits REMOVE so the peer's already-minted proxy despawns). Logs '[wi] ADOPT ...'.
- Known corner: simultaneous mutual adoption leaves the item authorless (correct visually; conservation blind until re-discovered). Watch ADOPT frequency in session logs.
- Both configs rebuilt + staged, contract 29/29. NOT deployed. Still protocol 46.

## 2026-07-29 - NPC wake-heal fix (recovered enemies fully healed on join)
- Cause: medNpc_ vitals qualification (fighting||fought||down) ENDED the instant a KO'd NPC stood up -> stream quit exactly at wake; plus join-side re-mints during the coma start from template = full health with nothing correcting them.
- Fix (host side, both in v46 builds): (1) medDownLatch_ - a body seen DOWN stays vitals-qualified 30 s past its last down sighting (wake grace); (2) answering a SPAWN_REQ stamps medNpc_[k] so freshly minted proxies get their real wounds within ~1 s.
- Both configs rebuilt + staged, contract 29/29. NOT deployed.

## 2026-07-30 - PROTOCOL 47: sky-refinery session forensics + four fixes
Live v46 session logs (host + Drew, clocks ~59.5 min apart) diagnosed:
1. SKY REFINERY: createBuilding treats Y as TERRAIN-RELATIVE (Drew sent abs 778.3, mint landed 1549.8 over ~771.5 terrain; harness fixture near y=0 hid it). Fix: placeBuildingAt self-calibrates - place at 0, read grounded Y, re-place with (want - grounded) if off by >1.5u ('mint height recal' log).
2. FROZEN PROXY PROGRESS: minted buildings' runtime hands do not re-resolve (every STATE-RECV ok=0). Fix: PeerBuild.obj keeps the Building* (lastMintedBuildingObj); STATE + REMOVE apply via writeBuildProgressPtr/destroyGroundItemPtr.
3. DELTA DEFEATED: every coop_risk transfer was mode=full 11.5MB/3min - Kenshi's save rewrite deletes files each time. Fix: v47 deletion tombstones - deleted rel paths ride the delta as SAVE_FILE chunks with offset=0xFFFFFFFF/fileIdx=0xFFFF/dataLen=0; receiver deletes at delta-commit (cap 256, full-send fallback).
4. PROXY-PICKUP CONSERVATION (iron plates report): a peer-authored non-gear proxy bagged locally notified nobody. Fix: publishWorldItems sweep - proxy no longer free (itemIsFreeGround==0) -> WORLD_TAKE to author + mapping erased immediately (so the author's later REMOVE can't delete the item from the picker's bag). WorldProxy gained sid.
- MINING GAP (task #6, deferred): machine state is host-authoritative one-way; join mining writes only local sim (ore invisible to host, later overwritten). Needs join->host PROD_DELTA intents. Animations on driven copies = separate cosmetic gap.
- PROTOCOL_VERSION 46 -> 47. Both must redeploy. Builds staged, contract 29/29. NOT deployed.

## 2026-07-30 (later) - v47 completed: PROD_DELTA join-side production sync
- PKT_PROD_DELTA (44): join detects local output INCREASES on BAKED machines (~1 Hz enumMachinesNear diff vs prodExpected_ baseline; own/minted buildings excluded) -> reliable delta to host; host lands it in publishProd (idempotent, materialize-if-null via setProductionItem, sanity cap 1000) and its normal stream echoes to both.
- Baseline maintenance: prodExpected_ adopted from every applied host row; SURPLUS GUARD in applyProd - a stale host row lower than local out on a baked machine forwards the surplus and suppresses the downward write (the mined-ore wipe race).
- v1 scope: baked machines only (deposits, town machinery). Session-placed machines still host-authoritative-only. Animations on driven copies remain a cosmetic gap.
- Also this build (earlier today): mint height self-calibration, PeerBuild.obj progress-by-pointer, delta save deletion tombstones, proxy-pickup WORLD_TAKE conservation. PROTOCOL 47. Builds staged, contract 29/29. NOT deployed.

## 2026-07-30 (night) - three new reports triaged (v46 session logs)
1. STONE DUP (task #7): [xfer] APPLY moved=0 fab=0 both directions on machine-container transfers (mine/processor <-> bags) - mirror-apply silently fails (suspect lazy machine-container Inventory on peer + prod-buffer/container-channel overlap), source copy survives = dup. Re-test on v47 before hardening (PROD_DELTA reshapes the flow).
2. EVENTS not synced (task #8b): Kenshi's world-event generator is per-machine; no channel exists anywhere. Consequences (caravan NPCs) sync via census/spawn; announcements/triggers don't. Research-grade.
3. FACTION NAME not synced (task #8a): only relations stream; the name string has no channel. Heals via save-transfer+reload on reconnect. Small poll+packet feature for next batch.
- Deliberately NO new code this turn: v47 (7 wire-affecting changes) is still unvalidated live - deploy and gather logs first.

## 2026-07-31 - first v47 live session feedback + completion-latch hardening
- CONFIRMED WORKING: refinery mints on the ground (height self-calibration).
- NEW BUG (suspected, logs pending): baked-site completion latch broadcast a false complete - host's copy of Drew's refinery completed at his 16%; host's Storm Shack finished instantly on material add. Hardened: (1) sender latch requires lastSent >= 0.85 before a leave-census complete may send; (2) BAKED-MERGE honors complete only with progress >= 0.99.
- CONFIRMED GAP: construction-site MATERWAL inventories still unsynced (plates into refinery invisible to host) - folded into task #7.
- Ground-pickup ghosts reported persisting on v47 - need both logs (ghostFb/TAKE/PROXY-PICKED lines will name the failing guard).
- Rebuilt both configs with the two guards; still protocol 47. NOT deployed.

## 2026-07-31 - SCALE DISCOVERY: constructionProgress = MATERIAL UNITS, not 0..1
- v47 session logs decoded the instant-complete bugs: localProg=6.000 (refinery) / 16.000 (storm shack) after completion - progress counts materials consumed, per-template requirement. The harness-era '>= 1.0 -> notifyConstructionComplete' rule in writeBuildProgressPtr force-completed real buildings at their FIRST material (Drew's plate -> 1.0 -> host completed at '16%' = 1/6). Same cascade finished the Storm Shack from a ~1.07 max-merge row on both sides.
- FIX: writeBuildProgressPtr(+ new writeBuildProgressByHandEx) take wantComplete - completion fires ONLY on the sender's engine-complete flag, never a threshold; wantComplete skips the raw progress write (engine sets its own full progress). STATE apply + BAKED-MERGE pass p.complete. Completion latch: replaced wrong-scale lastSent>=0.85 guard with TEMPLATE-SID IDENTITY guard (BakedRow.sid captured at tracking; leave-census hand must resolve to same sid). Legacy writeBuildProgressByHand untouched (harness fixtures rely on old rule).
- ALSO CONFIRMED WORKING in the session logs: mint-on-ground (height recal), STATE ok=1 real-time progress, TAKE-APPLY destroyed=1 (many), PROXY-PICKED->TAKE. STILL FAILING: PICKUP-APPLY ghostFb=0 moved=0 on clothes sids (likely CORPSE loot - dead-NPC inventories unsynced, new gap, task #7 family); construction-site material inventories (plates invisible) still task #7.
- Rebuilt both configs; protocol 47. NOT deployed.

## 2026-07-31 (later) - join world-spawn VETO at source (queue item 1)
- Implemented the join-side local world-spawn veto: detour on ZoneManager::spawnChecksUpdateThreaded (the ambient wildlife / roaming-squad ticker) in ZoneQuery.cpp (the quarantined-ZoneManager TU - EngineInternal cannot see the type). While armed the ticker is skipped entirely, so the join engine never generates squads the host does not have; Drew-class transient ghosts (spikes ~10 before supp catches up, plus ghosts persisting through census-stale starve windows) are prevented BEFORE they render instead of culled after.
- Arm gate (Plugin::tickReplicateApply, edge-logged '[spawn] join world-spawn veto ARMED/off (vetoedTicks=N)'): spawnVeto && !isHost && peerPresent && worldLive+gameplayLive. Host/solo never arm; peer loss or world swap disarms same tick. Detour installs pass-through whenever the knob is on (role can still be chosen at the title screen).
- Config: KENSHICOOP_SPAWN_VETO (default ON), 'spawnVeto' in describeConfig. Counter is interlocked (ticker runs on a zone worker thread).
- Fixed stale prototest expectations (SaveBeginPacket 67->68 v46 +delta; PROTOCOL_VERSION 45->47) - see buglog. NOW 435/435 PASS.
- No wire change (still protocol 47 - veto is purely local). Both configs rebuilt green + staged in builds/. NOT deployed; needs live join-session validation (watch vetoedTicks and whether ghost spikes disappear; escape hatch KENSHICOOP_SPAWN_VETO=0).
- Watch items: the ticker may also do despawn housekeeping (if joins accumulate stale distant wildlife, revisit - the census cull should still hide them); town repop paths (barflies, populateBuilding) are NOT vetoed in v1.

## 2026-07-31 (night) - town-flavor (barfly) veto added to the join spawn-veto regime
- Extended the spawn veto to town flavor population: barFlies_hook detour on Town::spawnTheBarFlies in ZoneQuery.cpp (Town.h coexists fine with ZoneManager.h there - the ParticlePool quarantine was ZoneManager-vs-CombatClass only). Same arm gate as the wildlife veto, separate knob KENSHICOOP_TOWN_VETO (default ON) + separate interlocked counter; edge log '[spawn] join town-flavor veto ARMED/off (vetoedBarflyRolls=N)'.
- SAFETY ARGUMENT (why join bars do not go empty): both players share ONE player faction, so the join squad members are player characters on the HOST engine too - Kenshi keeps zones loaded/simulated around all player squad members, so the host rolls barflies for the join's town as well and they arrive via census -> SPAWN_REQ -> proxy mint (the proven wildlife pipeline); recruit-sync (protocol 23) covers recruiting them. The join's local roll only ever produced ghost doubles the cull had to chase.
- Known cosmetic corner: join camera DETACHED from its squad viewing a distant town (host engine has no zones there) -> empty-ish bar; but the census cull already suppressed local barflies in that case, so the veto changes nothing material.
- Functional town population (populateBuilding/createCharacterForBuilding shopkeepers/guards) deliberately NOT vetoed - trading-with-proxy is unproven.
- No wire change (protocol 47). Both configs green + staged in builds/, prototest 435/435. NOT deployed. Validate live alongside the wildlife veto: watch vetoedBarflyRolls climb and that bars in shared towns show ONE population (host proxies), plus KENSHICOOP_TOWN_VETO=0 as the isolated rollback.

## 2026-07-31 (late night) - RESYNC button (RimWorld-MP-style full-state resync)
- F2 panel gains a "Resync: save + reload both worlds" button, shown ONLY when hosting with a live peer (real session state, not armed toggles; row appears/disappears on the peerPresent edge). UI stays session-agnostic: new CoopResyncFn callback param on coopPanelTick (signature changed - EngineUi.h), click drained end-of-tick with re-validation.
- Driver (Plugin.cpp tickCoordinatedSaveLoad, host): state machine idle->streaming->loading. Gates on issue: host + saveSync + loadSync + peer + no swap + !bootstrapArmed + no watch/send in flight. Chain: saveGameAs('coop_resync') -> normal protocol-31 quiescence/transfer -> completion = join ACK ok (lastAckXferId/lastAckOk) OR new savexfer::skipSeq() (XFER-SKIP: join copy already byte-identical, no ACK ever comes) -> loadSave('coop_resync') (retried; warnIfNoPortraits) -> load detour broadcasts LOAD_GO -> both reload the identical folder. Timeouts 300s xfer / +30s load; aborts on peer-leave or ACK-fail with '[resync] ...' log lines throughout.
- Robustness note (in code comment): if an autosave steals the pipeline mid-chain, the reload still converges - join NACKs the LOAD_GO fingerprint mismatch and driveLoadSync's fallback transfer re-streams before its load.
- savexfer::skipSeq() added (SaveXfer.h/cpp; counter lives in the sender-state block, accessor #ifndef KENSHICOOP_PROTOTEST - sender is not compiled for prototest).
- No wire change (protocol 47). Both configs green + staged, prototest 435/435. NOT deployed. Join-side "request resync" button = future follow-up (needs a small request packet or SAVE_REQ+glue).

## 2026-07-31 (late night 2) - F2 no longer unpauses (panel-toggle key guard)
- Field report: pressing F2 (co-op panel toggle) UNPAUSED a paused game - Kenshi's own keymap also reacts to the keypress. Fix: g_uiKeyGuardUntil (EngineInternal) - coopPanelTick arms a 350 ms window on every F2 rising edge; setGameSpeed_hook/userPause_hook/togglePause_hook swallow USER-originated writes inside the window (early return before intent recording, so speed-sync sees no vote either). Quiet sync writes (g_speedGuardWrite) always pass. Logged once per window: '[coop-ui] <fn> swallowed (panel-toggle key guard)'.
- The three speed-intent detours install unconditionally at gameplay start, so the guard covers host/join/solo alike.
- NOTE for live validation: if F2 still unpauses, the engine reached pause state WITHOUT those three entry points - the swallow log lines (present/absent) will say which.
- User also floated a clickable top-right button instead of F2: NOT currently feasible reliably - DatapanelGUI line-buttons are the only proven interactive MyGUI controls (EngineUi.h header note); floating ScreenLabels render but are not clickable. Would be UI research; F2 guard is the fix.
- Both configs green + staged, prototest 435/435. NOT deployed.

## 2026-08-01 - PROTOCOL 48: in-band DLL auto-update + MP log shipping (debug-stage tooling)
- NEW GENERIC CHANNEL: FilePush (sync/FilePush.{h,cpp}) - PKT_FILE_BEGIN/CHUNK/DONE/ACK (45-48) on CH_BULK, purpose-tagged (1=DLL, 2=LOG), paced sender (24 chunks/tick DLL, 6 log; 1 KB chunks), in-memory receiver with FNV-1a-32 verify + ACK. PKT_BUILD_INFO (49) on CH_RELIABLE. WIRE FORMAT FROZEN forever (with HELLO/WELCOME): it is the cross-version bootstrap subset the DLL update of a MISMATCHED join depends on. prototest sizeof checks added (Begin 20/Chunk 15/Done 9/Ack 10/BuildInfo 15); 440/440 PASS.
- DLL AUTO-UPDATE (KENSHICOOP_DLL_PUSH default ON): two triggers, host always pushes ITS build (host wins, downgrades included). (a) protocol mismatch: NetLink HELLO handler HOLDS the peer (id assigned, no WELCOME, no reject) when setDllPushHold armed and queues InboundVerMismatch; (b) same-protocol drift: join sends BUILD_INFO (own-DLL crc via core/SelfUpdate) after WELCOME, host compares. Push -> join filepush-receives -> selfupdate::applyImage rename dance (.new staged; live->.old rename works on a LOADED dll; .new->live; cleanupOld deletes .old next launch) -> '[update] ... RESTART Kenshi' + F2 panel banner (g_updateNotice overrides detail line). FIRST v48 build must still go to Drew by hand (his v47 has no receiver); after that updates flow in-band.
- MP LOG SHIPPING (KENSHICOOP_LOG_SHIP default ON, KENSHICOOP_LOG_SEG_MIN 15, KENSHICOOP_LOG_MAX_FILES 100): CoopLog now mirrors every line into <logdir>\KenshiCoopLogs\KenshiCoop_<ROLE>_<stamp>.log, rotating each segMin inside the log lock, pruning KenshiCoop_* to maxFiles; MAIN log untouched (oracles). JOIN ships finished segments over FilePush; delivered files rename to *.sent; on-connect rescan queues any unshipped *.log from PRIOR runs (crash survivors - fixes 'Drew's log overwritten on next launch'). Host stores under KenshiCoopLogs\peer\ (own prune). tickFilePush runs from mainLoop AND titleUpdate (menu-time updates; host-at-menu drives the held-peer push).
- Wiring: Inbound gained 6 session-preserving queues (fileBegin/Chunk/Done/Ack, buildInfo, verMismatch); NetLink gained queueFile*/queueBuildInfo + send drains + receive dispatch + setDllPushHold (armed in startNetworking: isHost && dllPush); peer-leave calls filepush::resetPeer + requeues in-flight segment; vcxproj +4 entries. PROTOCOL_VERSION 47 -> 48 (resources/PROTOCOL_HISTORY.md does not exist in this copy - this entry is the record).
- Both configs green + staged in builds/. NOT deployed. LIVE-VALIDATION NOTES: watch '[push]'/'[update]'/'[logship]' lines; first connect after both run v48 logs 'join build matches' (fingerprint path); DLL push test = stage a host-side rebuild, have Drew connect.

## 2026-08-01 (later) - first v48 live session TRIAGED VIA SHIPPED PEER LOGS; chest/site root causes fixed
- LOG SHIPPING PROVED LIVE: Drew''s segments arrived automatically in KenshiCoopLogs\peer\ (host got both sides'' evidence with zero manual steps). Papercut: peer files tagged HOST - the log tag is baked at logInit with the STARTUP role (Drew''s config defaults host; he picks JOIN at the F2 panel). Cosmetic; his timestamps are his clock (~59.5 min ahead).
- VETO CONFIRMED WORKING: '[spawn] join world-spawn veto ARMED', vetoedTicks=86610 over ~14 min on Drew. townVeto armed, vetoedBarflyRolls=0 (no town visited). Ground-pickup sync confirmed by user.
- FOUR FIXES from the logs (all built, staged, 440/440):
  1. ROLE-LATCH BUG (the big one): coopUiConnect re-derives ownRanks/streamNpcs but NOT setStoreSync/setReportCombat/setGateAuthority -> Drew (default-HOST cfg, panel-JOIN) census-authored world chests = DUAL-WRITER containers: his deposits were own->own (no intent authored) and neither side reconciled the other = deposit blackhole; his join-dealt combat report was also OFF and gate-authority OFF all along. Fixed: re-derive all three in coopUiConnect.
  2. CENSUS OWNERSHIP omitted in xfer paths: detect/apply srcOwn/dstOwn tests lacked censusContainers_ -> host authored bogus chest->chest intents + latched against its own authored chests ('[xfer] defer-expired ... unadjudicated local diff' churn). Fixed: census containers count as OWN in both.
  3. FAB QTY BUG: createItemAndAdd mints a 1-unit Item then tryAddItem(it, qty) -> every qty>1 fabrication failed (perfect correlation in logs: qty=2 moved=0 fab=0, qty=1 succeeded). Fixed: it->quantity = qty before add.
  4. SITE MATERIALS: enumContainersNear excluded incomplete sites -> construction-material inventories had no channel. Fixed: incomplete sites are authored regardless of container class (hasInv filters the rest); deposits ride census snapshots + xfer intents.
- PHANTOM DOWNED BANDIT (Drew ghost corpse): DEFERRED - census cull targets live NPCs; a ghost that goes DOWN before suppression likely leaves a lootable corpse (and non-vetoed sources: events/nests still spawn). Next step: probe whether listNpcsWide/getCharactersWithinSphere flags (96,96,0) include downed bodies; then a corpse-cull extension. Also corpse-loot sync still open (clothes PICKUP-APPLY ghostFb=0 lines again in this session).
- NOT deployed; both must redeploy (still protocol 48 - no wire change in these fixes, compatible with the first v48 handout).

## 2026-08-01 (later 2) - log tag follows the panel role
- coop::logSetTag added (CoopLog): coopUiConnect re-tags the log at the role flip, so a panel-JOIN client''s lines and NEW segment filenames say JOIN instead of the startup-default HOST (the peer\KenshiCoop_HOST_* confusion from the first v48 session). Log shipping itself was never affected (it gates on g_cfg.isHost, which always flipped). Rebuilt + staged, 440/440.

## 2026-08-01 (later 3) - pause-on-join fix, update auto-restart+reconnect, resync force-pause
1. PAUSE-ON-JOIN: Kenshi parks the engine PAUSED after a world load; the join seeded that as its VOTE and the min() arbitration paused the host on every join/reload. Fix (syncSpeed): the JOIN no longer seeds a vote from engine state - no REQ until a real user intent (combat-cap REQ still allowed at 1.0, = the cap itself); HOST keeps seeding (its engine state is authoritative). Join adopts host SETs meanwhile.
2. UPDATE AUTO-RESTART + RECONNECT: after a successful DLL apply AT THE MENU (join only; in-game keeps the banner), the updater writes coop_reconnect.tmp (steam=/peer=) beside the DLL, waits 1.5 s (ACK flush), selfupdate::relaunchGame() (CreateProcess with the ORIGINAL command line - two Kenshi instances coexist per the harness precedent) then TerminateProcess. Next launch: titleUpdate_hook consumes the flag, waits 3 s settle, coopUiConnect(join, transport, peer) - zero-input round trip. .old note: it IS the running module''s file (image-locked) - cannot delete in-session; cleanupOld removes it seconds later at the relaunch, so nothing lingers.
3. RESYNC FORCE-PAUSE: resync STARTED now issues writeGameSpeed(0, paused) - the LOUD path = the host''s pause VOTE -> consensus pauses BOTH worlds for the save+transfer; the coordinated load''s loading screen covers the rest; post-reload both start paused (players unpause when ready).
- Both configs green + staged, 440/440. Still protocol 48. NOT deployed (auto-push covers Drew once the host runs it).
