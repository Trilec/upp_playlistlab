# PLAYLISTLAB PROJECT STATUS

Updated: 2026-08-26

This document is a supervisory progress view. Percentages are milestone-weighted estimates, not line-count metrics. `docs/ACTIVE_WORK.md` remains the detailed technical continuation record and remote `main` remains authoritative.

## CURRENT CHECKPOINT

Current visual/interactions source:

`914674d2f9558875ae7b1935beb89637efb7ef68`

Last Windows-accepted source before those visual fixes:

`2d9fb4447034d3b881459b8ff39bc947da3a3e84`

That accepted baseline produced:

- `PlaylistCoreTest`: PASS — 95 checks;
- PlaylistLab CLANGx64 `GUI MT`: PASS;
- responsive root executable at `E:\apps\github\upp_playlistlab\build\PlaylistLab.exe`.

Current `upp_Ui/main` observed for the next validation gate:

`0554317f4b3136165b468b3db32b4bb74b2e2791`

Compared with the last accepted Ui dependency `6e77738f544fb83e0be58d2e3e92e7fceba57138`, the intervening five commits touch only `Ui/UiGraph/UiNodeGraph.h`; no PlaylistLab-used List/Button/Layout control source changed.

## PROGRESS

### Source implementation — 93%

Weighted implementation state:

| Area | Weight | Completion | State |
| --- | ---: | ---: | --- |
| Model, matching, planner and publication safety contracts | 25% | 100% | Implemented and core-tested |
| Spotify auth/read/search/artwork/client profiles | 20% | 95% | Main workflows implemented; true Spotify playback control remains a bounded follow-on |
| Working Playlist authoring/import/find/notes/drag/reorder | 25% | 95% | Implemented; latest selection/layout/delete/duplicate fixes await recompilation and visual acceptance |
| Create New / Append Missing / Replace destination operations | 15% | 90% | Guarded providers and previews implemented; disposable-playlist live acceptance still pending |
| UI simplification, layout, tooltips and interaction polish | 10% | 85% | Two-playlist model implemented; current visual-fix checkpoint awaits human review |
| Release validation, hygiene and documentation | 5% | 70% | Prior baseline accepted; current fix revalidation and final live-write acceptance remain |

Weighted source implementation estimate: approximately **93%**.

### Validation / release readiness — 82%

The lower release-readiness percentage deliberately reflects validation still outstanding:

- deterministic/core architecture: high confidence and previously accepted;
- current visual-fix source: not yet rebuilt on Windows;
- current visual interaction behavior: not yet human-accepted;
- Create / Append Missing / Replace: implemented but not yet exercised against a disposable Spotify playlist;
- true Spotify playback control: not yet implemented because it requires a new OAuth scope and should remain a separate bounded slice.

## COMPLETED PRODUCT SHAPE

PlaylistLab now uses two visible playlist concepts:

1. **Selected Spotify Playlist** — source and possible editable destination.
2. **Working Playlist** — local authored playlist and sole publication source.

Working can be populated by:

- Spotify Add Selected / Add All;
- Spotify-to-Working drag/drop;
- Spotify Find on Enter;
- CSV import;
- pasted text;
- explicit Resolve / candidate review.

Working supports:

- persistent editable name;
- artwork;
- local notes;
- multi-selection;
- request-first drag reorder;
- Delete-key removal;
- remove / clear / CSV export;
- compact match-state indicators;
- duplicate Spotify URI confirmation.

Destination actions are:

- **Create New**;
- **Append Missing** — occurrence-aware append only, no reorder/delete;
- **Replace** — explicit destructive exact replacement.

All Spotify writes show an exact preview first. The preview is the confirmation; there is no redundant second confirmation.

## CURRENT VISUAL FIX GATE

The latest source correction addresses Curt's current acceptance failures:

- icon-only commands use `UiToolButton` rather than squeezing normal `UiButton` content margins into 32 px;
- crowded command rows use `UiBoxLayout` with Fit/Expand/spacers instead of overlapping manual horizontal `SetRect` arithmetic;
- image track renderers remain List-owned so selected/hot row faces are visible;
- selected Spotify and Working rows use an obvious blue selected face/frame;
- Working Delete key raises a semantic delete request and the workspace performs the document mutation;
- exact Spotify duplicate additions prompt Keep/Cancel rather than silently duplicating;
- relevant icon/text commands have tooltips;
- the inspector command says **Open in Spotify**, matching its current URL-opening behavior.

## IMPLEMENTATION SCHEDULE

No calendar-duration promises are attached; work proceeds in bounded acceptance slices.

### Gate 1 — Current Windows rebuild and hold-open

Target: current `main` containing source `914674d2...`.

Required:

- refresh current remote PlaylistLab and Ui heads;
- run `PlaylistCoreTest` and report actual count;
- build PlaylistLab CLANGx64 `GUI MT`;
- place final executable directly at `build\PlaylistLab.exe`;
- launch/hold open if environment permits;
- no Spotify writes.

Expected project state after PASS: **~86% release readiness**.

### Gate 2 — Curt visual / interaction acceptance

Check:

- icon size and tooltips;
- no command-row overlap;
- obvious single/multi-selection state;
- Delete removal;
- duplicate Keep/Cancel flow;
- search width and command-row balance;
- drag/reorder behavior;
- inspector/Open in Spotify behavior.

Fix only concrete acceptance failures found here.

Expected project state after PASS: **~91% release readiness**.

### Gate 3 — True Spotify playback control

Bounded follow-on:

- add the required Spotify playback OAuth scope deliberately;
- use Spotify's player API to start the selected track on the active device;
- do not implement this as separate pause + URL-launch commands;
- handle Premium/device/no-active-device cases clearly;
- retain **Open in Spotify** as fallback/navigation where appropriate.

Expected project state after PASS: **~94% release readiness**.

### Gate 4 — Disposable Spotify write acceptance

Use a disposable playlist/account target and test in increasing risk order:

1. Create New;
2. Append Missing;
3. Replace.

For each operation verify:

- preview accuracy;
- snapshot/stale protection;
- exact final readback;
- partial/error messaging;
- no hidden retry/replan/rollback;
- user-visible result in Spotify.

Expected project state after PASS: **~99% release readiness**.

### Gate 5 — Closure

- final Windows core/build pass;
- final GUI smoke/human review;
- documentation and `ACTIVE_WORK` closure;
- remove temporary validation artifacts/branches;
- optional release/tag decision.

Expected project state: **100% of the current PlaylistLab milestone**.

## NOT IN THE CURRENT CRITICAL PATH

These are not blockers for the current two-playlist milestone unless explicitly promoted:

- restoring the retired visible Imported Playlist;
- reintroducing the old placement/order-mode UI;
- arbitrary playlist deletion workflows;
- embedded Spotify audio streaming inside PlaylistLab;
- broad redesign of shared `upp_Ui` controls where PlaylistLab usage is the actual defect.

## NEXT ACTION

Run Gate 1 against current remote truth, then Curt performs Gate 2 visual acceptance before additional feature work.