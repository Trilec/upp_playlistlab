# ACTIVE WORK

CURRENT SOURCE: `914674d2f9558875ae7b1935beb89637efb7ef68`
LAST WINDOWS-ACCEPTED SOURCE: `2d9fb4447034d3b881459b8ff39bc947da3a3e84`
CURRENT `upp_Ui/main`: `6e77738f544fb83e0be58d2e3e92e7fceba57138`
TASK: PL-002 — simplified two-playlist PlaylistLab workspace
STATUS: VISUAL/INTERACTION ACCEPTANCE FIXES PUBLISHED; WINDOWS RECOMPILE + HUMAN VISUAL RECHECK REQUIRED

## CURRENT PRODUCT MODEL

PlaylistLab has two visible playlist surfaces:

1. **Selected Spotify Playlist** — provider/source/target context.
2. **Working Playlist** — local authored state and the only publish-authoritative list.

CSV import, clipboard text, and Spotify Find add directly to Working. CSV/Paste do **not** silently authorize or match against Spotify; the user presses **Resolve** explicitly when ready. Ambiguous matches remain review state until explicitly confirmed.

`PlaylistLab/PlaylistWorkspace.cpp` is the sole production GUI entry listed by `PlaylistLab.upp`. The retired monolithic `PlaylistLab/main.cpp` is absent.

## LAST VERIFIED WINDOWS ACCEPTANCE

Gary validated PlaylistLab source:

`2d9fb4447034d3b881459b8ff39bc947da3a3e84`

against current `upp_Ui/main`:

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

Do not carry this acceptance forward to the current visual-fix source until it is recompiled.

## CURT VISUAL ACCEPTANCE — 2026-08-26

The accepted executable exposed several GUI/interaction failures. Root causes were inspected against exact current `upp_Ui` source before editing.

### 1. ICON-ONLY BUTTONS APPEARED AS DOTS / SLIVERS

Affected examples:

- Spotify Client profile add/edit/refresh;
- rename beside Open Spotify;
- Working remove/clear beside Export CSV.

Root cause was **PlaylistLab misuse**, not the shared icon scaler.

`UiButton` is a regular command button and its default metrics reserve `DPI(14)` horizontal content margin on each side. PlaylistLab then forced these icon-only buttons into roughly 30–32 px widths. That leaves only a few pixels of content width, so the requested 16–17 px icon was necessarily compressed into a sliver.

`UiToolButton` is the intended icon/chrome control and uses compact/zero content margin. The affected icon-only commands now use `UiToolButton` at 32 px with 18 px icons. `upp_Ui` defaults were **not** changed.

All icon-only controls retain explicit `.Tip(...)` help text. Text commands that benefit from explanation also now have tooltips.

### 2. TRACK SELECTION WAS NOT VISUALLY DISTINGUISHABLE

Root cause was also PlaylistLab-side.

PlaylistLab had assigned a custom style to `UiItemRenderImage` merely to alter artwork size. In `UiList`, a custom-styled item renderer deliberately owns its own row style and therefore no longer receives the List-owned selected/hot face projection. This detached Selected Spotify and Working rows from `UiList::selected_face`.

The custom renderer style has been removed. The image renderer is again List-owned, so selection state flows through the normal `UiListOwnedItemRenderStyle` path.

Selected Spotify and Working rows now additionally use a deliberately obvious dark-blue selected face, blue frame, and white selected text. Ctrl/Shift multi-selection remains enabled.

No `upp_Ui` selection fix was required.

### 3. COMMAND ROWS COULD OVERLAP

The overlap came from PlaylistLab's manual `SetRect` width arithmetic plus hard minimum widths. Under constrained centre-panel width, those minima could mathematically overlap the right-anchored controls.

The crowded command surfaces now use `UiBoxLayout` rather than hand-maintained horizontal coordinates:

- Spotify Client profile selector row;
- Selected Spotify Playlist header/action row;
- Working Playlist name/destination row;
- Working search/import/action row.

The layouts use semantic `Fit`, `Expand`, bounded widths, expanding spacers, and light vertical separator spacers. Search is bounded to a smaller width instead of consuming the whole free row.

The outer application/panel geometry remains manually positioned; only the command rows that benefit from flow ownership were converted.

### 4. DELETE KEY

`PlaylistTransferList` now exposes a semantic `WhenDeleteRequest` event when Delete is pressed with a Working selection. `PlaylistWorkspace` handles that event through the existing `RemoveWorkingSelected()` document mutation path.

This preserves the request-first architecture: the list never mutates `PlaylistDocument` itself.

The remove icon button remains available, but Delete is now the direct keyboard path.

### 5. DUPLICATE TRACK ADDITIONS

Exact Spotify duplicates are no longer inserted silently.

Before Spotify Add Selected/Add All/drag, Spotify Find, or an imported document containing already-resolved Spotify URIs is committed to Working, PlaylistLab checks exact publishable Spotify URIs against:

- existing Working occurrences; and
- earlier occurrences in the same proposed add.

If duplicates are found, one confirmation asks whether to keep the duplicate occurrence(s). Cancel leaves Working unchanged.

Duplicates are not prohibited outright because repeated songs can be intentional playlist/set-list content. Unresolved text-only rows are not guessed to be duplicates by title string.

### 6. OPEN IN SPOTIFY SEMANTICS

The inspector command is now labelled **Open in Spotify**, not Play in Spotify.

Its current behavior remains opening the track's Spotify URL. The tooltip explicitly states that this does not control or replace the active Spotify playback session.

True stop/current-playback replacement is a separate Spotify Connect/Web API capability and must not be implied by this URL-opening command. Do not silently add playback-control scopes during this GUI correction.

## CURRENT FIX COMMITS

Acceptance-fix commits after the last Windows-accepted source:

- `404445fbe15cb5bc374ce16d5be6bdb09a5cac01` — semantic Delete request in `PlaylistTransferList`;
- `914674d2f9558875ae7b1935beb89637efb7ef68` — box-layout command rows, UiToolButton icon commands, visible selection, duplicate confirmation, tooltips, and Open-in-Spotify semantics.

Diff from accepted source `2d9fb444...` to current source touches exactly:

- `PlaylistLab/PlaylistTransferList.h`;
- `PlaylistLab/PlaylistWorkspace.cpp`.

`PlaylistLab.upp` already contains both and requires no membership change.

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

Append Missing is occurrence-aware append-only behavior. It stable-reads the target/snapshot, rejects stale state, computes missing Working occurrences, appends those URIs in Working order, stable-reads the final target, and requires exact `original target + planned additions` before success.

No reorder and no deletion occur in Append Missing.

### Replace

Replace is explicitly destructive. It requires an editable loaded target, exact publishable Working sequence, preview confirmation, fresh snapshot preflight, and exact final readback.

### Preview behavior

Create / Append / Replace show an exact preview. Cancel performs no write. Stale/partial states are surfaced and there is no hidden retry/replan/rollback.

## LEGACY GUARDED PLANNER

The deterministic reference-slot/reference-first planner and guarded Apply provider remain core-tested infrastructure, but the placement/order-mode UI is no longer the primary product workflow. Do not reintroduce the retired third-list / Apply-order UI without a new explicit product decision.

## PACKAGE / DEPENDENCY TRUTH

Current `PlaylistLab.upp` includes the model, IO, planner, local state, Spotify auth/client split sources, image cache, `PlaylistTransferList.h`, and `PlaylistWorkspace.cpp`; mainconfig remains `GUI MT`.

Current remote `Trilec/upp_Ui/main` used by the last accepted build is:

`6e77738f544fb83e0be58d2e3e92e7fceba57138`

No `upp_Ui` source change was made for the current visual fixes. Inspection showed the icon, selection, and overlap failures were caused by how PlaylistLab was using otherwise intentional control contracts.

## BUILD OUTPUT CONVENTION

Intermediate U++ objects may live under:

`E:\apps\github\upp_playlistlab\build\_obj`

The final executable for Curt must be directly at:

`E:\apps\github\upp_playlistlab\build\PlaylistLab.exe`

## NEXT ACTION

Windows compile/test agent:

1. refresh exact current `origin/main` and report SHA;
2. refresh/verify current `Trilec/upp_Ui/main` and clean dependency worktree;
3. run `PlaylistCoreTest` — expected historical count is 95, but report the actual current result;
4. build PlaylistLab CLANGx64 `GUI MT` with final output at `build\PlaylistLab.exe`;
5. run `git diff --check` and confirm clean worktree/no unrelated files;
6. if build succeeds, leave the root executable ready for Curt's manual visual pass;
7. do not perform live Spotify mutations.

Curt's next visual recheck should focus on:

- icon size and tooltip behavior for all icon-only commands;
- no overlap in the Spotify and Working command rows;
- clear single/multi-selection face in Selected Spotify and Working lists;
- Delete-key removal and remove-button behavior;
- duplicate warning/cancel/keep behavior;
- search field width;
- Open in Spotify wording and behavior.
