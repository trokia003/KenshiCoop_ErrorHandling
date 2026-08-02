# KenshiLib_deps local patches (KenshiCoop)

The vendored `third_party/KenshiLib_deps/` clone
([KenshiLib_Examples_deps](https://github.com/BFrizzleFoShizzle/KenshiLib_Examples_deps))
needs two things beyond a plain clone to build KenshiCoop with the VC2010
(v100) toolchain:

## 1. Pin to the 0.3.0-layout commit

```
git -C third_party/KenshiLib_deps checkout e75769b
```

KenshiCoop's includes use the flat header layout (`<kenshi/CombatClass.h>`).
The deps repo's `b566d74` ("Update KenshiLib to 0.4.0", 2026-06-24) moved that
header to `kenshi/combat/`, which breaks the build. `e75769b` (0.3.0 + boost
env fix) is the newest compatible commit. Also extract boost once:
`tar -xf boost_1_60_0/boost.zip --directory boost_1_60_0/` (or run the deps
repo's own `Setup.bat`).

## 2. Apply `0001-vc10-header-fixes.patch`

```
git -C third_party/KenshiLib_deps apply ../kenshilib_patches/0001-vc10-header-fixes.patch
```

Three header fixes, all compile-only (no ABI/layout change for anything the
plugin touches):

- **`kenshi/Platoon.h` + `kenshi/Building/Building.h`**: both define the
  identical `enum BuildingDesignation`; any TU including both (EngineInternal.h
  does) fails with C2011 under VC10. Both copies are wrapped in one shared
  include guard.
- **`kenshi/Building/CraftingBuilding.h`**: `CraftingItem` was only
  forward-declared, but `CraftingBuilding` has a `std::deque<CraftingItem>`
  member and VC10 requires the element type complete at class completion
  (C2027). Replaced with an opaque placeholder body — KenshiCoop never
  constructs, copies, or dereferences `CraftingItem` elements (verified: zero
  references in `src/`), so only completeness matters, not layout.

The upstream KenshiCoop maintainer's builds work against a local deps copy
that evidently carries equivalent fixes; these patches reproduce that state
for a fresh clone. If upstream deps ever ship these fixes, drop this folder.
