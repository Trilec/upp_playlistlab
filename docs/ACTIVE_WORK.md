# ACTIVE WORK

## STATE

TASK: `PL-002-FINAL-WIN-R3`

STATUS: **SOURCE COMPLETE; WINDOWS RECOMPILE + CURT VISUAL ACCEPTANCE PENDING**

PlaylistLab substantive source checkpoint:

`c2aeed24b89700a68c2e2c1fdd14b9ead4bba89a`

Current `Trilec/upp_Ui/main` observed at freeze:

`ae753860f6a61fdc9cff3f0cfa6733ab27d13f32`

Last fully Windows-accepted PlaylistLab source remains:

`2d9fb4447034d3b881459b8ff39bc947da3a3e84`

That earlier acceptance completed **95 PlaylistCoreTest checks**, CLANGx64 `GUI MT` compile/link, clean worktree, and a responsive `PlaylistLab.exe`. Do not carry that acceptance forward to the current source until the new build runs.

Remote GitHub `main` is authoritative. Refresh both repositories before validation. `upp_Ui/main` is an actively advancing shared dependency: the validation agent must record and use current clean `origin/main`; do **not** stop merely because its SHA has advanced from the checkpoint above. Stop only for a dirty/diverged checkout or a substantive compile/API regression.

## PRODUCT MODEL

PlaylistLab exposes two playlist surfaces:

1. **Selected Spotify Playlist** — current Spotify source/target context.
2. **Working Playlist** — local authored state and the only publish-authoritative list.

Spotify tracks, Find, CSV and pasted text feed Working directly. Imported/pasted rows remain non-publishable until their match state allows publication. Resolve is explicit rather than silently authorizing/searching.

`PlaylistLab/PlaylistWorkspace.cpp` is the sole production GUI entry. The retired `PlaylistLab/main.cpp` is absent.

## CURRENT FINALISATION SLICE

### Application identity

The repository-root `icon.png` is now the single PlaylistLab visual identity.

It is embedded into the executable through `PlaylistLab/PlaylistResources.brc`, decoded by `PlaylistLabAppIcon()` in `PlaylistAppIdentity.cpp`, and used for:

- the native PlaylistLab window/application icon; and
- the left media slot of the main `UiTitleCard` beside the PlaylistLab title.

The helper falls back to the existing generic Ui widgets glyph only if embedded PNG decoding unexpectedly fails. No runtime repository/file path is required.

### Main workspace UI

Implemented and published:

- top-right **Help / Light-Dark Theme / Exit** header actions using the established `UiTitleCard` content-cell pattern;
- runtime separator colors refresh when switching theme;
- `UiToolButton` for compact icon-only commands;
- tooltips for icon-only and ambiguous actions;
- box-layout command rows instead of overlapping manual horizontal geometry;
- obvious Selected Spotify and Working selection faces;
- Working Delete-key removal through a semantic document-owner request;
- exact Spotify duplicate warning before insertion;
- inspector follows selection from either Spotify or Working;
- Spotify-source inspector shows artwork/title/artist/album/duration/identity and can surface an existing local Working note for the same Spotify URI;
- Working notes remain local/editable and are never sent to Spotify;
- clearer compact add/edit/refresh/remove/clear/rename glyphs;
- search field width reduced to leave toolbar breathing room.

### Help / Spotify setup

`PlaylistLab/PlaylistHelp.cpp` uses **UiDoc** with semantic headings and a theme-correct document page/frame/ink palette. It explains:

- sign into Spotify in the browser first;
- open Spotify Developer Dashboard;
- create/open the PlaylistLab developer app;
- obtain Client ID from the app Basic Information page;
- register exactly `http://127.0.0.1:43821/callback`;
- PlaylistLab never needs/stores Client Secret;
- Development Mode currently requires Premium for the app owner;
- Client ID itself does not expire;
- current refresh-token authorization lifetime is about six months / 180 days;
- Account Apps is useful for connected-app review but is not where Developer Client ID is shown;
- current PlaylistLab Create / Append Missing / Replace semantics and troubleshooting.

Help buttons open Developer Dashboard, Account Apps and Spotify's February 2026 API changes, and can copy the redirect URI.

## REQUIRED UPP_UI BEHAVIOR IN THIS BUILD

The visual failures seen in the previous executable were traced to reusable control contracts and corrected in current `upp_Ui/main` rather than patched around in PlaylistLab.

### UiButton icon-only sizing

Current `upp_Ui` includes the icon-only UiButton contract correction: explicit icon size is preserved by yielding icon-only padding before shrinking, and natural/minimum sizing reflects icon-only content. PlaylistLab still uses `UiToolButton` for true toolbar/icon chrome.

### UiBoxLayout cross-axis clamp

Horizontal non-wrapping rows now clamp children to the assigned cross-axis height. This is the expected fix for the clipped button/split-button borders previously seen in the 30px Spotify/Working command rows.

### UiItemRenderImage right-decoration ownership

Horizontal image rows reserve metadata as a right-side decoration before laying out right text. The Working green/blue/amber/red match marker should therefore be vertically centred and the duration should sit to its left rather than underneath it.

Do not add PlaylistLab-specific geometry workarounds for these corrected shared-control contracts unless the current build still demonstrates a failure.

## DESTINATION SAFETY

Working destination actions remain:

- **Create New** — create a new private Spotify playlist from exact publishable Working order;
- **Append Missing** — occurrence-aware append only, no reorder/delete;
- **Replace** — explicit destructive replacement to exact Working order.

Each action uses an exact preview. Cancel performs no write. Stale/partial outcomes are surfaced. No live Spotify mutation is authorized during the compile gate.

## PLAYBACK

The current inspector action remains **Open in Spotify** because that is exactly what the current implementation does.

Spotify supports true selected-track playback through the Player Web API (`PUT /me/player/play`), but implementing it correctly requires `user-modify-playback-state`, reauthorization of existing profiles, active-device handling, and Premium failure handling. That is the next bounded feature after this visual acceptance rather than a mislabeled URL-launch action.

## LOCAL AUDIO REFERENCE IDEA — DEFERRED

Windows system-output/WASAPI loopback can technically capture system playback, but PlaylistLab must not become a Spotify ripping/downloading path. A future **Record Reference** feature may capture only audio the user has the legal right to record and associate local reference media with title/artist/album/artwork/Spotify URI/notes. Spotify's Web API itself does not provide raw downloadable audio.

## PACKAGE / HYGIENE TRUTH

Current package includes:

- `PlaylistAppIdentity.h/.cpp`;
- `PlaylistHelp.h/.cpp`;
- `PlaylistWorkspace.cpp`;
- `PlaylistResources.brc` depending on `PlaylistAppIdentity.cpp`;
- the existing model/planner/local-state/Spotify split sources.

`PlaylistResources.brc` embeds `../icon.png` as `playlistlab_icon_png`.

Repository branch inventory at freeze: **main only**.

No obsolete feature branch is part of the current workflow.

## BUILD OUTPUT CONVENTION

Final executable must be directly:

`E:\apps\github\upp_playlistlab\build\PlaylistLab.exe`

Intermediate objects may live under `build\_obj`.

## NEXT VALIDATION

Validation agent:

1. refresh exact current `origin/main` for PlaylistLab and `upp_Ui`;
2. require clean worktrees; record actual SHAs;
3. use current clean `upp_Ui/main` even if it has advanced from `ae753860...`;
4. run relevant Ui regression tests, especially:
   - `UiButtonInteractionContractTest`;
   - `UiListStyleContractTest`;
   - `UiModelViewPerformanceTest`;
   - relevant current layout/List package/demo compilation;
5. run PlaylistLab `PlaylistCoreTest` — historical accepted count is 95, report actual current count;
6. build PlaylistLab CLANGx64 `GUI MT` with final output at `build\PlaylistLab.exe`;
7. run `git diff --check`, verify clean worktree and package/resource membership;
8. launch/hold responsive if execution environment permits;
9. perform **no** Spotify Create / Append Missing / Replace mutation.

Curt's visual check should focus on:

1. custom PlaylistLab icon appears as the application/window icon and beside the main title;
2. button/split-button borders are fully visible in Spotify/Working command rows;
3. Working duration sits left of a vertically centred match-state marker;
4. Help body is readable in dark and light themes with semantic headings;
5. Help/theme/Exit header controls and tooltips work;
6. Track Details updates from either Spotify or Working selection;
7. selection visibility, Delete removal, duplicate warning and drag/drop still function;
8. source/working list artwork and title/metadata spacing remain clean.
