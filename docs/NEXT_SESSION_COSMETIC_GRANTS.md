# Handoff — cosmetic grants, obtainability, and the SNO group table

**Scope: finish the cosmetic *grant* model and ship the group-table fix. Everything in section 2 is
already measured — do not re-derive it.** The decisions in section 6 were made deliberately; if one
looks wrong, say so and stop rather than "improving" it.

Written 2026-10-08. Game build measured against: `3.2.1.73552`.

---

## 1. Directories

| What | Path |
|---|---|
| Project root | `G:\G Claude Projects\AssetBrowser\Diablo4AssetBrowser Native` |
| Source | `...\Diablo4AssetBrowser Native\src` |
| Build output | `...\Diablo4AssetBrowser Native\build\release` |
| d4data snapshot | `...\Diablo4AssetBrowser Native\build\release\data\d4data` |
| TACT keys | `...\Diablo4AssetBrowser Native\.Resources` |
| Wiki clone | `G:\G Claude Projects\AssetBrowser\D4AssetBrowser.wiki` |
| Game install | `G:\G Games\Diablo IV` (product `fenris`) |
| Backups | `G:\G Claude Projects\AssetBrowser\.Backups` |

The folder move off `C:\Users\notso\Downloads\Claude Current` is **complete**: the ini, the TACT
key path and `CMAKE_HOME_DIRECTORY` all point at G. Do not re-run `Fix Paths After Move.bat`.

Scripts in the project root: `rebuild.bat`, `build.bat`, `clean-rebuild.bat`, `backup-src.bat`,
`github.bat`, `Push Wiki.bat`.

---

## 2. What is already established — measured, not assumed

### 2.1 The grant graph: what decides whether the game will ever show a cosmetic

The art chain runs downward and answers *what does this look like*:

```
Item → snoActor → Actor → snoAppearance → Appearance → Material → Texture
```

It cannot answer *will a player ever see this*. That is decided **upward**, by whether any record
references the Item at all:

```
StoreProduct ──snoItemTransmog──> Item     (shop / pass catalogue entry)
Achievement  ────────────────> Item     (reward grant)
Reputation   ────────────────> Item
Quest        ────────────────> Item
PlayerClass  ──arItemTiers[]────> Item     (dropped-gear roster, see 2.3)
```

A StoreProduct grants one ownable thing through whichever typed slot applies: `snoItemTransmog`,
`snoMount`, `snoEmote`, `snoEmblem`, `snoHeadstone`, `snoTownPortal`, `snoHairStyle`,
`snoFacialHair`, `snoJewelry`, `snoMarkingShape`, `snoCompanion`, `snoPower`, `snoDyeArmor`. All
null = a product that grants nothing.

Grantor group ids, all confirmed from reference objects: **110** StoreProduct, **99** Achievement,
**105** Reputation, **31** Quest, **74** PlayerClass. A product's season is a **137** Season
reference on `snoAssociatedSeason`.

### 2.2 `bIsTransmog` — one canonical wardrobe entry per appearance

Measured across all **12,117** `.itm` records: **2,466 true / 9,651 false**. Those items reference
**7,034** distinct Actors. Of those 7,034: **2,466 have exactly one `true` item, 4,568 have zero,
and none has two.** No exceptions in the whole dataset.

**CORRECTED 2026-10-09 — the interpretation that stood here was wrong.** It read `bIsTransmog` as
*the canonical wardrobe entry for the appearance* and the 4,568 actors with none as drop proxies.
Measured since:

- The one-per-actor invariant holds in all three builds checked (2.6.0: 2,101 true / 0 actors with
  two; 3.0.1: 2,298 / 0; 3.2.1: 2,466 / 0). That part stands.
- Of the 4,546 Items a StoreProduct grants through `snoItemTransmog`, **3,062 are false**. Shop
  cosmetics that players wear carry a false flag, so it is not "in the wardrobe".
- Of the 4,568 actors with no true item, 3,061 are store-granted and only 465 are `*_flippy`.
- Hunch, unverified: it gates unlocking a look from the *item itself* (found / salvaged), which
  shop entitlements do not need. Does not explain the 1,484 store-granted Items that are true.

The true item's own filename matches its actor's name **1,591** times and differs **875** times.
It holds for shop cosmetics and breaks on base gear: `1HAxe_Magic_Generic_002` carries the flag for
actor `axe_base01`.

### 2.3 Salvaged gear has no per-item grantor

Referencing groups across the 2,466 transmog items: PlayerClass 2,235, StoreProduct 1,484, Item 435,
Actor 428, Recipe 132, MarkerSet 77, **Achievement 47**, Quest 14, Reputation 13. Forty-seven
achievements against 2,466 items rules out a hidden achievement per salvage.

Dropped gear's roster is `PlayerClass.arItemTiers[]` — **131 tiers per class**, each carrying
`snoHead` / `snoTorso` / `snoHands` / `snoLegs` / `snoFeet`. 695 item references on Barbarian, 730 on
Rogue, 347 on Warlock. Salvaging records the appearance account-side; the client only needs the
item-to-appearance mapping, which is what `bIsTransmog` provides.

### 2.4 Mounts bypass the flag entirely

All **154** items with `snoMount` have `bIsTransmog = false`, and all 154 share **one** `snoActor`:
`Test_Mount_Dev_Reins_Item`, a dev placeholder. `snoMount` points at a *different* Actor (group 1),
e.g. `Mount_Cat.acr`.

So nothing is overriding anything. The flag indexes the `snoActor` appearance; mounts don't reach
their cosmetic that way, so it has nothing to mark — and the one-per-appearance rule could not apply
regardless, since all 154 would contend over the same placeholder. Mount **trophies** are a third
case: `mnt_uniq28_trophy` has `bIsTransmog = true` and uses `snoActor` normally.

### 2.5 Orphans = de-listed, and it is derivable

Across all **463** `*trophy*.itm`: 375 granted by StoreProduct, 34 by Achievement, 3 by Reputation,
1 by Quest, **55 by nothing**. The 55 are the retired set — they include both dev test trophies
(`trophy_glo998_test`, `trophy_glo999_test`), the whole `_hibernal_lte` event group and every `_pvp`
trophy. Their art, Actor, Appearance and name strings are all still shipped and all parse clean.
**A cosmetic is retired by cutting the grant, never by deleting assets.**

**CORRECTED 2026-10-09:** the last sentence of the next paragraph is too broad. 437 leaf products
have every slot null; only 23 share a name with an Item, and ~400 are licences, tracking dummies,
Platinum cards and passes. `mnt_amor103_horse_stor` (Demonbinder) has had an all-null product since
2023-10 including while it was usable. All-null means "grants nothing through a typed slot" only.

Two de-listing patterns: product file deleted (`mnt_uniq28_trophy`, *Blood Maiden’s Mantle*, S4), or
product kept with `snoItemTransmog: null` (`mnt_uniq29_trophy`, *The Felled Council*). **A
StoreProduct with every `sno*` slot null is a retired product, not a failed decode.**

### 2.6 The SNO group table was wrong in 85 of 132 entries

`json/snoGroups.json` is the game's own group id→name table, 167 entries, ids `-3`..`181`. Validated
before adoption: 22 of 24 `base/meta/<Folder>` directories agree exactly (the two exceptions are
group 6's folder being `Anim` vs the table's `Animation`, and `PlayerClass` 74 having no unambiguous
stem to score); and 64 distinct `(id, groupName)` pairs read off sno references inside 1,770 records
agree with **zero** disagreements.

**36 is ShaderMap, 37 is Shader.** The page's old claim came from a test that could not decide it:
"every shader name is in 37 and absent from 57" is true and equally true of 36, since the two groups
carry nearly identical names at adjacent SNO ids (`hero_hair` = 290837 in 36, 290899 in 37). A
material's own `tUberMaterial.snoShaderMap` declares `__group__ 36`, `groupName "ShaderMap"`, target
`base/meta/ShaderMap/<name>.shm`, in 60 of 60 sampled materials.

---

## 3. The one open contradiction — resolve this first

The question that started this came from a third party who reported: *"I have seen `bIsTransmog`
switch when I add and remove cosmetics from the wardrobe."*

**That cannot be true of the shipped `.itm`** — it is static CASC data, identical bytes for every
player, and section 2.2 measured it as a fixed one-per-appearance invariant. Either they are reading
live memory or save state rather than the file, or they are comparing two different `.itm` files
that share an appearance.

**ANSWERED 2026-10-09.** They meant *between patches*, and they are right. Demonbinder
(`mnt_amor103_horse_stor`) is true in every d4data snapshot from 2023-06 through 2.6.0.70982, false
at 3.0.1.71566, true at 3.2.1.73552 — the builds they named. Whole-folder diff: 2.6.0 → 3.0.1
switched 49 true → false and 23 false → true; 3.0.1 → 3.2.1 switched 11 off and 2 on. For 48 of the
49, no sibling took the flag. So the flag is static within a build and is patch data across builds.
See the correction in 2.2 for what it does and does not mean.

---

## 4. Open work, in order

1. **Commit and push the code repo.** `SnoIndex.cpp`, `CHANGELOG.md`, `rebuild.bat`,
   `Push Wiki.bat` and `tools/verify-sno-groups.py` are all uncommitted. Use `github.bat`, and apply
   the `commit-attribution` skill (roughly 75% Claude / 25% trappuss as author, the other as
   `Co-Authored-By`, randomised per commit).
2. **Decide `excludedGroups()`.** See section 6 — a product decision, deliberately left undone.
3. **Correct the `d4browser-gamedata` skill.** It almost certainly carries the same wrong group ids
   that `SnoIndex.cpp` did, including ShaderMap at 37. Needs a `propose_skills` proposal with the
   complete updated `SKILL.md`; the skill file on disk is a read-only cache and editing it does
   nothing.
4. **Paladin off-hand shield floats.** Still unfixed. `Dev - Build d4shader + Probe.bat` was written
   to answer it from data and has never been run — it dumps `ptHardpoints` from the `.app.json`
   files. The candidates are `HP_shield` (1410210544) and `HP_leftWeapon` (4036545548). Run the
   probe before theorising.
5. **An *Unobtainable* filter for the Catalogue** — **blocked, no sound rule yet.** Both candidate
   rules fail measurement: "all-null product" flags licences and Platinum (see 2.5), and "no
   `bIsTransmog` item" flags 3,061 purchasable appearances (see 2.2). The nearest thing is "no true
   item AND no grantor on any item" — 617 actors, holding 31 of the 39 known switched-off ones — but
   it also holds crafted base gear. Do not build it without a decision on the definition.
6. **Fix the 2.4.1 GitHub release.** Empty body and a divergent title — CI published it instead of
   `github.bat`. Regenerate with `python release-notes.py 2.4.1 RELEASE_BODY.md`, paste it, rename
   the title to `D4AssetBrowser_v2.4.1`, confirm the attached assets.
7. **Delete the orphaned `src\tabs\CollectionsTab.cpp`.** Left over from the removed Collections
   tab. Deleting through the device bridge needs `device_request_delete_permission` first.
8. **The rest of the group-label audit is closed** by 2.6. Evidence is in `sno_group_census.tsv`.

---

## 5. Current state — do not redo

- `src/index/SnoIndex.cpp` group table **replaced and built**. `build_errors.txt` is empty,
  `D4AssetBrowser.exe` relinked 2026-10-08 19:04 with the new `SnoIndex.cpp.obj`.
- `tools/verify-sno-groups.py` added and **wired into `rebuild.bat`**, non-blocking, beside
  `verify-src.py`. Tested against a deliberately corrupted copy: it catches mislabels and duplicate
  names and exits 1.
- `CHANGELOG.md` has a new `## Unreleased` section covering all of the above.
- Wiki **pushed** as `a2809c3` — `Asset-formats.md` sections 3, 5 and 13 and the §2 correction, plus
  `Glossary.md` gaining `Grantor`, `Orphan / de-listed` and `Shader`, and `ShaderMap` moved to 36.
- `Push Wiki.bat` added to the project root (not to the wiki clone, so it is never pushed to the
  wiki itself).

---

## 6. `excludedGroups()` — flagged, deliberately not changed

`src/index/SnoIndex.cpp` has `{2, 29, 31, 104, 166}`, inherited from the Python fork as
"Powers (29/104), Quests (31/166), Adventure (2)". Against the corrected table: 29 is Power
(10,293 records) and 31 is Quest (3,732) as intended, but **104 is Affix (6,329 records)**, 2 is
NpcComponentSet (255) and 166 is BehaviorContainer (3). There is no Adventure group at all; the real
second power group is 176 PowerModifier (44) and the real QuestChain is 96 (94), neither excluded.

So the browser hides 6,329 Affix records nobody meant to hide and shows the quest chains it believed
it was hiding. **Changing the ids changes what the browser lists**, which is a product decision, not
a label fix. Ask before touching it. Do not "tidy" it.

---

## 7. Querying d4data without wasting a session

`device_bash` is available on this machine now, which removes the old `device_stage_files` depth-7
cap — `json/enUS_Text/meta/StringList` (8 deep, 61,330 files) is reachable directly. But the mount is
slow and each call has a hard ~180 s ceiling, so:

- **`grep -r` over `base/meta` times out.** So does `grep -l` over the 7,786 StoreProduct files.
  Never reach for it.
- **Use the shipped indexes instead.** `json/incomingSnoReferences.json` (52 MB) answers "what
  points at this?", `outgoingSnoReferences.json` (48 MB) the reverse, both keyed by SNO id over the
  whole TOC, and verified consistent with the records. `CoreTOC_flat.json` is `sno → [name, group]`.
  `snoGroups.json` is the group table.
- **Scanning all 12,117 Items takes ~4 chunked calls.** Full `json.load` per file is too slow; read
  bytes and regex out only the fields you need, 3,000 files per call, dump each chunk to a file
  under `$HOME` and merge. That is how section 2.2 was measured.
- Every sno reference carries `__group__`, `groupName` and `__targetFileName__`. That is the
  record's own answer about what it is, and the only thing that can settle a two-candidate group
  question — see 2.6.

---

## 8. Standing constraints

- **No guessing.** Work from evidence through to the conclusion. If something is placed on a hunch,
  mark it as a hunch and ask. Write the number down with the claim.
- **No rushing.** Correct beats fast; do not leave a trail of fixes for fixes.
- **Never run git in these folders through a file bridge or mounted share** — it leaves
  `.git/index.lock` behind and the bridge cannot delete it. Run `github.bat` / `Push Wiki.bat`
  natively instead.
- **Do not add a `.gitattributes`** to this repo. 163 commits of line endings are already committed.
- **Line endings are mixed per file and must be preserved.** `SnoIndex.cpp`, `CHANGELOG.md`,
  `CatalogueTab.*`, `WardrobeTab2.*`, `MainWindow.*`, `README.md`, `github.bat`, `Push Wiki.bat`
  are CRLF; `SeriesIndex.*`, `ProductContents.h`, `GLModelWidget.*`, `CascReader.*`, `rebuild.bat`,
  `tools/*.py` and the wiki `.md` files are LF. **Reading a CRLF file in Python text mode and
  writing it back silently converts it to LF** — always load and save bytes with an explicit
  restore, and assert the result.
- **Stage the device's copy of a pre-existing file before editing it.** A stale mirror copy once
  clobbered `CMakeLists.txt`, reverting its version and re-adding a `find_package(fastgltf)` that
  had been deliberately removed.
- Anything the user has to run should be a `.bat`. Do not drive their PC.
- Report concisely.
