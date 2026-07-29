# Cerebrum â€” KenshiCoop_ErrorHandling

## Preferences
- Never commit, push, or take any GitHub action (issues/PRs/API writes) without the user's express say-so. Editing files is fine.
- Code comments: keep them about the invariant, not the war story â€” avoid session-specific dates/details in header comments (user trimmed the 2026-07-22 crash narrative from CrashGuard.h; the codebase's inline "run NNNNN" idiom is fine where it explains a guard).

## Learnings
- This working copy is F:\GameModding\KenshiCoop_ErrorHandling, branch CrashHandling (upstream repo: nhoral/KenshiCoop; user is a contributor, not maintainer).
- Plugin must build with VC++ 2010 (v100) x64 (KenshiLib ABI). scripts/build_plugin.cmd expects VS10 at C:\Program Files (x86)\Microsoft Visual Studio 10.0 + SDK 7.1 â€” NOT currently installed; third_party fetched deps (KenshiLib_deps, enet clone) missing from this copy as of 2026-07-25. VS2022 Community on F: works for /c syntax checks of standalone files only.
- User's Kenshi install: A:\SteamLibrary\steamapps\common\Kenshi (logs: KenshiCoop_host.log etc.).
- `rebirth.mod`, `Dialogue.mod`, `chareditor.mod`, `gamedata.quack` are VANILLA Kenshi data files â€” never misread them as user mods in logs.

## Do-Not-Repeat
- Do not run git commit/push/gh without explicit user instruction (standing rule).
- Do not assume the crash logs' .mod sids imply a modded game (see vanilla data files above).

