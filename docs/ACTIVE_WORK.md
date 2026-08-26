# ACTIVE WORK

## STATE

TASK: `PL-002-FINAL-WIN-R2`

STATUS: **SOURCE COMPLETE; WINDOWS RECOMPILE + CURT VISUAL ACCEPTANCE PENDING**

PlaylistLab source checkpoint:

`de7c47541028d4a2e39d936719b8d486618163ac`

Required `Trilec/upp_Ui/main` checkpoint:

`5f6d9c0a3b2e093603b016c1b247c38f8b661460`

Last fully Windows-accepted PlaylistLab source:

`2d9fb4447034d3b881459b8ff39bc947da3a3e84`

That earlier acceptance completed **95 PlaylistCoreTest checks**, CLANGx64 `GUI MT` compile/link, clean worktree, and a responsive `PlaylistLab.exe`. Do not carry that acceptance forward to the current source until the new build runs.

Remote GitHub `main` is authoritative. Refresh both repositories before any validation or source work.

## PRODUCT MODEL

PlaylistLab exposes two playlist surfaces:

1. **Selected Spotify Playlist** — current Spotify source/target context.
2. **Working Playlist** — local authored state and the only publish-authoritative list.

Spotify tracks, Find, CSV and pasted text feed Working directly. Imported/pasted rows remain non-publishable until their match state allows publication. Resolve is explicit rather than silently authorizing/searching.

`PlaylistLab/PlaylistWorkspace.cpp` is the sole production `GUI_APP_MAIN`. The retired `PlaylistLab/main.cpp` is absent.

## CURRENT FINALISATION SLICE

### Application UI

Implemented and published:

- top-right **Help / Light-Dark Theme / Exit** header actions using the established `UiTitleCard` content-cell pattern;
- `UiToolButton` for compact icon-only commands;
- tooltips for icon-only and ambiguous actions;
- box-layout command rows instead of manual overlapping horizontal geometry;
- obvious Selected Spotify and Working selection faces;
- Working Delete-key removal through a semantic document-owner request;
- exact Spotify duplicate warning before insertion;
- inspector follows selection from either Spotify or Working;
- Spotify-source inspector shows artwork/title/artist/album/duration/identity and can surface an existing local Working note for the same Spotify URI;
- Working notes remain local/editable and are never sent to Spotify;
- source/working command-row spacing adjusted so the list frame is separated from the command controls;
- clearer compact add/edit/refresh/remove/clear/rename glyphs;
- runtime separator colors refresh when switching light/dark theme.

### Help / Spotify setup

`PlaylistLab/PlaylistHelp.cpp` now uses **UiDoc**, not a plain multiline edit.

The guide has semantic H1/H2 headings and an explicit theme-correct document page/frame/ink palette. It explains:

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

## REQUIRED UPP_UI FIXES IN THIS BUILD

Two latest visual failures were traced to reusable control contracts and fixed in `upp_Ui/main` rather than patched around in PlaylistLab.

### `d34b49f1ab45aa40fbe8451ab93a45946ff855d1`

**UiBoxLayout non-wrap cross-axis clamp**

A horizontal non-wrapping box could derive `row_h` from a child's preferred height even when the parent assigned a smaller row. The child could therefore extend outside the layout rectangle and have its button/split-button frame clipped.

A non-wrapping horizontal row now owns exactly the available inner cross-axis height. Fit/Center/End children are clamped to it and Stretch fills it.

### `5f6d9c0a3b2e093603b016c1b247c38f8b661460`

**UiItemRenderImage right-decoration ownership**

The image renderer previously placed metadata at the top-right after duration/right-text layout, so Working's green match marker visually sat above the duration.

Horizontal image rows now reserve metadata as a right-side decoration lane first, vertically center the marker, and place right text immediately to its left. The marker and duration no longer overlap.

## DESTINATION SAFETY

Working destination actions remain:

- **Create New** — create a new private Spotify playlist from exact publishable Working order;
- **Append Missing** — occurrence-aware append only, no reorder/delete;
- **Replace** — explicit destructive replacement to exact Working order.

Each action uses an exact preview. Cancel performs no write. Stale/partial outcomes are surfaced. No live Spotify mutation is authorized during the compile gate.

## PLAYBACK

The current inspector action remains **Open in Spotify** because that is exactly what the implemented code does.

Spotify does support true selected-track playback through the Player Web API, but implementing it correctly requires adding player-control OAuth permission(s), reauthorization of existing profiles, and handling active-device/Premium failure states. That is a separate bounded feature after this visual acceptance rather than a mislabeled URL-launch action.

## LOCAL AUDIO REFERENCE IDEA — DEFERRED

A future local **Record Reference** feature could technically use Windows system-output/WASAPI loopback capture and associate a locally recorded reference with the selected track's title/artist/album/artwork metadata. Spotify's Web API itself does not provide raw downloadable audio. Any recording feature must be limited to audio the user has the legal right to record and must respect Spotify/content licensing terms. It is not part of the current playlist milestone.

## BUILD OUTPUT CONVENTION

Final executable must be directly:

`E:\apps\github\upp_playlistlab\build\PlaylistLab.exe`

Intermediate objects may live under `build\_obj`.

## NEXT VALIDATION

Refresh exact current `main` in both repositories, then validate the source checkpoints above.

Required regression/build checks:

- `upp_Ui` `UiListStyleContractTest`;
- `upp_Ui` `UiModelViewPerformanceTest`;
- relevant Ui List/layout demo/package compilation;
- PlaylistLab `PlaylistCoreTest` — previous count was 95; report actual current count;
- PlaylistLab CLANGx64 `GUI MT` compile/link;
- `git diff --check` and clean worktree;
- final executable at the root build path above;
- launch and leave responsive if execution environment permits.

No Spotify Create / Append Missing / Replace mutation during this gate.

Curt's visual check should focus on:

1. button/split-button borders fully visible in the 30px command rows;
2. Working duration shifted left of a vertically centred match-state marker;
3. Help body readable in dark and light themes with semantic headings;
4. Help/theme/Exit header buttons and tooltips;
5. Track Details updating from either Spotify or Working selection;
6. selection visibility, Delete removal, duplicate warning and drag/drop still functioning.
