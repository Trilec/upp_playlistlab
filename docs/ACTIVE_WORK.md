# ACTIVE WORK

CURRENT SOURCE: `dbe31a8d791dd5df8d959abdc305905421c26492`
LAST WINDOWS-ACCEPTED SOURCE: `2d9fb4447034d3b881459b8ff39bc947da3a3e84`
CURRENT `upp_Ui/main`: `7d5ac627b5ab0202c2754c0f5c5c130a9d4bb448`
TASK: PL-002 — simplified two-playlist PlaylistLab workspace
STATUS: FINAL SOURCE CHECKPOINT PUBLISHED; WINDOWS RECOMPILE/TEST + CURT VISUAL ACCEPTANCE REQUIRED

## CURRENT PRODUCT MODEL

PlaylistLab has two visible playlist surfaces:

1. **Selected Spotify Playlist** — provider/source/target context.
2. **Working Playlist** — local authored state and the only publish-authoritative list.

CSV import, clipboard text, and Spotify Find add directly to Working. CSV/Paste do **not** silently authorize or match against Spotify; the user presses **Resolve** explicitly when ready. Ambiguous matches remain review state until explicitly confirmed.

`PlaylistLab/PlaylistWorkspace.cpp` is the sole production GUI entry listed by `PlaylistLab.upp`. The retired monolithic `PlaylistLab/main.cpp` is absent.

## FINAL SOURCE CHECKPOINT — 2026-08-26

Source checkpoint:

`dbe31a8d791dd5df8d959abdc305905421c26492`

This closes the current visual/interaction correction pass.

### Header actions

The far-right PlaylistLab title-card actions now follow the established `upp_Ui` demo convention:

- Help;
- runtime Light/Dark theme toggle;
- Exit.

They are compact `UiToolButton`s with explicit icons and tooltips inside a horizontal `UiBoxLayout` hosted by `UiTitleCard::SetContentCell(...)`.

### Spotify setup Help

`PlaylistHelp.h/.cpp` provides an in-app setup/usage dialog covering:

- signing in to Spotify in the normal browser first;
- Spotify Developer Dashboard navigation;
- creating/opening the PlaylistLab developer app;
- locating and copying the Client ID;
- exact redirect URI `http://127.0.0.1:43821/callback`;
- friendly PlaylistLab Client ID profiles;
- no Client Secret requirement/storage;
- Spotify Development Mode Premium requirement;
- six-month/roughly 180-day user refresh-token lifetime;
- Client ID itself does not expire;
- February 2026 API changes;
- two-list PlaylistLab workflow and troubleshooting.

The Help dialog includes direct actions for the Developer Dashboard, Account Apps, February 2026 API changes, and copying the redirect URI.

### Track inspector

Track Details now follows the current track selection from **either** visible playlist.

For Selected Spotify Playlist tracks it shows artwork/title/artist/album/time/source identity. If the same Spotify URI has an existing local Working note, that note can be displayed while inspecting the Spotify source track, but source-side notes remain read-only.

For Working tracks the inspector retains editable local PlaylistLab notes and contextual Review Match behavior. **Open in Spotify** follows whichever track is currently inspected; it remains navigation rather than Spotify playback control.

### Layout / selection / icon correction

Current source includes:

- `UiBoxLayout` ownership for the crowded profile/Spotify/Working command rows;
- bounded smaller search width;
- extra Working-row/list spacing so the list frame is not clipped by the command row;
- clear selected-row face/frame/ink for Selected Spotify and Working lists;
- semantic Delete-key removal through `WhenDeleteRequest`;
- duplicate Spotify-URI confirmation before adding duplicate occurrences;
- `UiToolButton` for compact icon-only commands;
- clearer remove/clear glyphs;
- explicit tooltips for compact icon actions.

The two vertical separator spacer handles are retained explicitly. Runtime theme changes recolour those exact separator items, so they no longer retain the colour authored under the previous theme.

## LAST VERIFIED WINDOWS ACCEPTANCE

Gary previously validated PlaylistLab source:

`2d9fb4447034d3b881459b8ff39bc947da3a3e84`

against `upp_Ui`:

`6e77738f544fb83e0be58d2e3e92e7fceba57138`

Results:

- `PlaylistCoreTest`: PASS — **95 checks completed**, exit code 0, no failures;
- PlaylistLab CLANGx64 `GUI MT`: compile PASS, link PASS, no diagnostics;
- package hygiene: PASS;
- exactly one production `GUI_APP_MAIN`: `PlaylistWorkspace.cpp`;
- `git diff --check`: clean;
- PlaylistLab worktree: clean;
- final executable: `E:\apps\github\upp_playlistlab\build\PlaylistLab.exe`;
- process launched and remained responsive;
- no live Spotify writes were performed.

This acceptance is historical only. Do **not** carry it forward to `dbe31a8d...` until the final Windows compile/test task passes.

## CURRENT `upp_Ui` DEPENDENCY

Current remote `Trilec/upp_Ui/main` is:

`7d5ac627b5ab0202c2754c0f5c5c130a9d4bb448`

This is ahead of the last accepted PlaylistLab dependency and now includes the shared icon-only `UiButton` sizing correction from:

`d52c96b5464e01ec584ee80c4579803efcb966d8`

That shared fix gives explicit icon-only `UiButton` sizing a compact-content contract and preserves requested icon size until the styled face genuinely cannot contain it. PlaylistLab still uses `UiToolButton` for toolbar/icon-only actions where that is the semantically correct control.

The current dependency therefore also needs Windows compilation as part of the final PlaylistLab acceptance. Do not pin back to the older accepted `upp_Ui` solely because it compiled previously unless a new regression is diagnosed.

## WORKING PLAYLIST

Working supports:

- editable playlist name;
- Ctrl/Shift multi-selection with visible selected-row state;
- Spotify -> Working copy by button or drag/drop;
- source split-button with persistent **Add Selected / Add All** default;
- duplicate warning for exact Spotify URI occurrences;
- local right-grip drag reorder with request-first document mutation;
- Find on Enter, CSV import, Paste Text, explicit Resolve, Export CSV;
- Delete key or remove button for selected rows;
- clear-all-tracks while preserving Working name;
- artwork and compact match-state markers;
- contextual Review Match for amber rows;
- right-side inspector with title/artist/album/time/state, Open in Spotify, and local notes.

`TrackEntry::user_note` is separate from resolver/match `note`. It is cloned and persisted in `playlistlab.working.json`, survives resolution resets, and is never sent to Spotify. Inspector notes are committed before reorder, resolve, review, remove, clear, close, and other index-changing operations.

## SPOTIFY CLIENT PROFILES

PlaylistLab stores friendly-name + Client ID profiles in `playlistlab.clients.json`. The compact profile strip exposes selector, add, edit, and refresh. Add/Edit uses a dialog containing Friendly Name and Spotify Client ID; Delete is inside the edit dialog.

No Client Secret is requested or stored. Changing Client ID clears tokens belonging to the previous client through `SpotifyAuth`.

## SPOTIFY READ ACCESS

Playlist metadata visibility, item readability, and editability are separate.

`SpotifyPlaylistInfo` tracks `items_access_checked` separately from `items_accessible` and `editable`.

Library state:

- `EDIT` — editable playlist;
- `CHECK` — noneditable playlist whose item endpoint has not yet been proven;
- `SOURCE` — noneditable playlist whose item list was successfully read;
- `META` — actual Spotify 403/404 from the playlist-items path.

Fresh playlist metadata is not preclassified as readable. Selection attempts the real item request. Only success promotes read access; only 403/404 records metadata-only. Transient network/token errors do not permanently mark a playlist META. Publish controls remain governed by editability.

## DESTINATION ACTIONS

The Working destination split-button remembers the selected default.

### Create New

Creates a new private Spotify playlist from exact publishable Working order. Partial population is reported rather than presented as atomic success.

### Append Missing

Occurrence-aware append-only behavior. It stable-reads the target/snapshot, rejects stale state, computes missing Working occurrences, appends those URIs in Working order, stable-reads the final target, and requires exact `original target + planned additions` before success.

No reorder and no deletion occur in Append Missing.

### Replace

Explicitly destructive. It requires an editable loaded target, exact publishable Working sequence, preview confirmation, fresh snapshot preflight, and exact final readback.

### Preview behavior

Create / Append / Replace show an exact preview. Cancel performs no write. Stale/partial states are surfaced and there is no hidden retry/replan/rollback.

## LEGACY GUARDED PLANNER

The deterministic reference-slot/reference-first planner and guarded Apply provider remain core-tested infrastructure, but the placement/order-mode UI is no longer the primary product workflow. Do not reintroduce the retired third-list / Apply-order UI without a new explicit product decision.

## PACKAGE / BRANCH TRUTH

`PlaylistLab.upp` includes the model, IO, planner, local state, Spotify auth/client split sources, image cache, `PlaylistTransferList.h`, `PlaylistHelp.h/.cpp`, and `PlaylistWorkspace.cpp`. `mainconfig` remains `GUI MT`.

Remote PlaylistLab branch hygiene is **main-only** at this checkpoint; no extra project branches need cleanup.

## BUILD OUTPUT CONVENTION

Intermediate U++ objects may live under:

`E:\apps\github\upp_playlistlab\build\_obj`

The final executable for Curt must be directly at:

`E:\apps\github\upp_playlistlab\build\PlaylistLab.exe`

Do not leave the final executable buried in U++ configuration subdirectories.

## NEXT TASK — PL-002-FINAL-WIN

Windows compile/test acceptance only. The agent should not redesign or broaden the source slice.

Required actions:

1. fetch `Trilec/upp_playlistlab` and verify exact current `origin/main`; report the SHA before work;
2. fetch `Trilec/upp_Ui` and verify current `origin/main`; report its SHA before work;
3. ensure both local worktrees are clean before building;
4. run `PlaylistCoreTest` and report exact check/failure counts;
5. build PlaylistLab CLANGx64 `GUI MT` against current `upp_Ui/main`;
6. final executable must be `E:\apps\github\upp_playlistlab\build\PlaylistLab.exe`;
7. run `git diff --check` and confirm package membership / one production `GUI_APP_MAIN` / no temporary or unrelated tracked files;
8. if compilation exposes a **tiny obvious mechanical** issue, fix it minimally, commit it on `main`, report old/new SHA, and rerun the failed validation; stop and report instead of redesigning if the correction is substantive;
9. if build succeeds, launch the root executable and leave it open/responsive for Curt's manual visual inspection;
10. do **not** perform live Spotify mutations.

Curt's visual acceptance should focus on:

- Help, theme and Exit header actions;
- Help dialog readability and Spotify setup links/copy action;
- runtime dark/light theme, including vertical separator colours;
- icon scale and tooltip behavior for compact icon commands;
- no overlap/clipping in Spotify and Working command rows;
- visible single/multi-selection in Selected Spotify and Working lists;
- Track Details from either list;
- Working notes editable vs Spotify-source notes read-only;
- Delete-key/remove/clear behavior;
- duplicate warning cancel/keep behavior;
- destination split-button default persistence and exact preview flow.
