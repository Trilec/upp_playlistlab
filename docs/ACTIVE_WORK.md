# ACTIVE WORK

IMPLEMENTATION SOURCE: `416ca74640740a42cdb8d96a441dda05471658e1`
SIMPLIFIED-SLICE BASE: `9b23446a501e79c449f796350552dd83d7a2d33e`
HISTORICAL TESTED CHECKPOINT: `1989b0e9ff9e92d8c52a0b283d0706eedb11bc9a`
TASK: PL-002 — simplified two-playlist PlaylistLab workspace
STATUS: SOURCE / HYGIENE CLOSED; WINDOWS COMPILE + CORE TEST VALIDATION REQUIRED

## CURRENT PRODUCT MODEL

PlaylistLab now has two visible playlist surfaces:

1. **Selected Spotify Playlist** — provider/source/target context.
2. **Working Playlist** — local authored state and the only publish-authoritative list.

The retired visible Imported Playlist has been removed. CSV import, clipboard text, and Spotify Find add directly to Working. CSV/Paste do **not** silently authorize or match against Spotify; the user presses **Resolve** explicitly when ready. Ambiguous matches remain review state until explicitly confirmed.

The old monolithic `PlaylistLab/main.cpp` has been deleted. `PlaylistLab/PlaylistWorkspace.cpp` is the sole GUI entry listed by `PlaylistLab.upp` and contains the single `GUI_APP_MAIN`.

## WORKING PLAYLIST

Working supports:

- editable playlist name;
- Ctrl/Shift multi-selection;
- Spotify -> Working copy by button or drag/drop;
- source split-button with persistent **Add Selected / Add All** default;
- local right-grip drag reorder with request-first document mutation;
- Find on Enter, CSV import, Paste Text, explicit Resolve, Export CSV;
- remove selected / clear tracks while preserving the Working name;
- artwork and compact match-state markers;
- contextual Review Match for amber rows;
- right-side inspector with title/artist/album/time/state, Play in Spotify, and local notes.

`TrackEntry::user_note` is separate from resolver/match `note`. It is cloned and persisted in `playlistlab.working.json`, survives resolution resets, and is never sent to Spotify. Inspector notes are committed before reorder, resolve, review, remove, clear, close, and other index-changing operations so notes cannot migrate to the wrong row.

## SPOTIFY CLIENT PROFILES

PlaylistLab stores friendly-name + Client ID profiles in `playlistlab.clients.json`. The compact profile strip exposes selector, add, edit, and refresh. Add/Edit uses a dialog containing Friendly Name and Spotify Client ID; Delete is inside the edit dialog.

No Client Secret is requested or stored. Changing Client ID clears tokens belonging to the previous client through `SpotifyAuth`.

## SPOTIFY READ ACCESS

Playlist metadata visibility, item readability, and editability are deliberately separate.

`SpotifyPlaylistInfo` now tracks `items_access_checked` separately from `items_accessible` and `editable`.

Library state:

- `EDIT` — editable playlist;
- `CHECK` — noneditable playlist whose item endpoint has not yet been proven;
- `SOURCE` — noneditable playlist whose item list was successfully read;
- `META` — actual Spotify 403/404 from the playlist-items path.

Fresh playlist metadata is not preclassified as readable. Selection attempts the real item request. Only success promotes read access; only 403/404 records metadata-only. Transient network/token errors do not permanently mark a playlist META. Publish controls remain governed by editability.

This matches Spotify's current Web API distinction: a playlist can be visible in `/me/playlists` or the Spotify client while third-party item enumeration may still be refused unless the current user owns/collaborates on it.

## DESTINATION ACTIONS

The Working destination split-button remembers the selected default:

### Create New

Creates a new private Spotify playlist from exact publishable Working order. If playlist creation succeeds but population stops, PlaylistLab reports the partial result rather than pretending atomic success.

### Append Missing

Append Missing is a real append-only operation, not a renamed reorder planner.

Pure deterministic helper:

`BuildAppendMissingUris(working_uris, target_uris)`

It is occurrence-aware and preserves Working order for missing duplicate occurrences.

Provider execution:

1. stable-read current target + snapshot;
2. reject stale expected snapshot;
3. compute missing occurrences;
4. append only those URIs at the end;
5. stable-read final target;
6. require exact `original target + planned additions` before success.

No reorder and no deletion occur in Append Missing.

### Replace

Replace remains explicitly destructive. It requires an editable loaded target, exact publishable Working sequence, preview confirmation, fresh snapshot preflight, and exact final readback. The preview is now the confirmation dialog; there is no redundant second confirmation prompt.

### Preview behavior

Create / Append / Replace all show an exact preview. Cancel performs no write. The positive action uses a restrained green pulse. Mutation failures are treated conservatively; stale/partial states are surfaced and no hidden retry/replan/rollback occurs.

## LEGACY GUARDED PLANNER

The deterministic reference-slot/reference-first planner and guarded Apply provider remain core-tested infrastructure, but the placement/order-mode UI is no longer the primary product workflow. Do not reintroduce the retired third-list / Apply-order UI without a new explicit product decision.

## PACKAGE / SOURCE HYGIENE

Current `PlaylistLab.upp` includes:

- `PlaylistModel.*`
- `PlaylistIO.*`
- `PlaylistPlanner.*`
- `PlaylistLocalState.*`
- `SpotifyAuth.*`
- `SpotifyClient.*`
- `SpotifyClientSearch.cpp`
- `SpotifyClientAppend.cpp`
- `SpotifyClientReplace.cpp`
- `SpotifyClientDetails.cpp`
- `SpotifyImageCache.*`
- `PlaylistTransferList.h`
- `PlaylistWorkspace.cpp`

`main.cpp` is removed. The repository tree contains no second GUI entry source.

Declaration/definition closure checked for Create, search/resolve, guarded Apply, UpdatePlaylistDetails, ExecuteAppendMissing, and ExecuteReplaceItems. Append planner declarations/definitions and core tests are package-visible through the existing `PlaylistLab` dependency.

## CORE TEST CHANGES

`PlaylistCoreTest` retains the historical deterministic planner/matching coverage and adds tests for:

- occurrence-aware Append Missing with duplicates and order preservation;
- no-op append planning;
- empty-target append planning;
- preservation of `user_note` through `CloneTrackEntry` and `ClearResolution` while resolver evidence is cleared.

The previous accepted run was **88 checks** at the historical checkpoint above. The expanded current test has **not yet been run on Windows**, so record the actual new check count from validation rather than assuming it.

## UI DEPENDENCY

Expected exact `upp_Ui/main` dependency for compile validation:

`91f2926f57a86dfc6df08d9b0ae10173085dcbf5`

This is the previously accepted UiList/Ui item-render baseline used by PlaylistLab.

## VALIDATION BOUNDARY

Current implementation source `416ca746...` is source-reviewed and package-closed but **not yet Windows compile/test accepted**.

Historical evidence only:

- PlaylistCoreTest: PASS — 88 checks at `1989b0e9...`;
- PlaylistLab CLANGx64 `GUI MT`: compile/link PASS at that older checkpoint;
- final executable convention previously validated.

Do not carry those results forward as current validation.

No live Spotify Create/Append/Replace mutation is required for the compile gate and none should be performed by the compile agent.

## BUILD OUTPUT CONVENTION

Intermediate U++ objects may live under:

`E:\apps\github\upp_playlistlab\build\_obj`

The final executable for Curt must be directly at:

`E:\apps\github\upp_playlistlab\build\PlaylistLab.exe`

## NEXT ACTION

Windows compile/test agent:

1. refresh exact current `origin/main` and report SHA;
2. verify `upp_Ui` dependency SHA;
3. run `PlaylistCoreTest` and report exact check/failure count;
4. build PlaylistLab CLANGx64 `GUI MT` with final output at `build\PlaylistLab.exe`;
5. run `git diff --check` and confirm clean worktree / no unrelated files;
6. if build succeeds, launch and hold PlaylistLab open if that execution environment can display it;
7. do not perform live Spotify mutations.

Curt then performs the human-visible GUI review from the root build executable.
