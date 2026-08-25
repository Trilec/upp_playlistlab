# ACTIVE WORK

SOURCE BASE: `c205094486097f1aecb7067781d304677e8e7b8e` (`main` source-closure checkpoint)
TESTED CHECKPOINT: `1989b0e9ff9e92d8c52a0b283d0706eedb11bc9a`
TASK: PL-001 — completed PlaylistLab workspace and guarded Spotify destination workflows
STATUS: WINDOWS COMPILE/TEST ACCEPTED; HUMAN GUI / OPTIONAL LIVE SPOTIFY VALIDATION NEXT

## CURRENT PRODUCT STATE

PlaylistLab has three separate playlist concepts with explicit ownership:

1. **Spotify Playlist** — selected Spotify target/source context and its readable items.
2. **Imported Playlist** — CSV, clipboard, or typed Spotify Find staging with match/review/missing evidence.
3. **Working Playlist** — the only editable/publish-authoritative document used for local reorder, Apply, Create New Playlist, and explicit Replace.

Spotify and Imported lists support Ctrl/Shift multi-selection, Add Selected/Add All, artwork, and copy drag/drop into Working. Working remains semantic authority for reorder; `UiList` internal mutation is disabled and reorder is applied to `PlaylistDocument` before rebuilding the projection. Empty Working remains an active drop target. The Working drag grip is on the right and multi-selection is preserved when a selected source row begins a transfer drag.

Imported Playlist supports Import CSV, Paste Text, Export CSV, Resolve, Review Match, Remove, Clear, and typed Spotify **Find**. Enter in the Find editor invokes the same Spotify search as the Find button. Search results are staged in Imported and do not become publish authority until explicitly copied into Working.

## CLIENT / LOCAL STATE

PlaylistLab stores friendly-name + Spotify Client ID profiles in `playlistlab.clients.json`; no Client Secret is requested or stored. `SpotifyAuth` remains token authority for the selected Client ID. Changing Client ID clears tokens belonging to the previous client.

Working state and source label persist in `playlistlab.working.json`; UI target/order state persists in `playlistlab.ui.json`; Spotify auth state persists in `playlistlab.spotify.json`; artwork is cached under `playlistlab-cache`. Working state preserves candidates and match state so unresolved/review/missing evidence survives restart.

## SPOTIFY DESTINATIONS AND SAFETY

Only `TRACK_EXACT` / `TRACK_AUTO` expose publishable Spotify identity. Review/Missing/Unresolved rows remain blocked.

**Apply to Selected Playlist** retains the guarded non-destructive publisher. It executes the exact stored preview after a fresh target/snapshot read, appends only missing occurrences, preserves unrelated/unavailable positions, performs the exact stored reorder plan, uses evolving snapshots, treats failed mutations conservatively, and succeeds only after exact final readback. Apply never deletes or replaces target items.

**Create New Playlist** creates a private Spotify playlist and populates it from the exact publishable Working order. The worker owns its URI sequence through member-held pending state rather than capturing a U++ `Vector` by value. If creation succeeds but population stops, PlaylistLab reports that partial state and reloads playlist metadata rather than pretending the operation was atomic.

**Replace Selected Playlist** is a separate explicitly destructive path. It requires an editable loaded target, exact before/after preview, destructive warning, explicit second confirmation, fresh target/snapshot preflight, exact desired Working order, and exact final readback. It is not silently folded into Apply. `SpotifyClient::ExecuteReplaceItems` replaces/clears the first 100 URIs with `PUT /playlists/{id}/items`, appends any tail in guarded batches, recovers observed state when possible, and reports stale/partial/observed evidence.

No delete/replace/rollback behavior exists inside guarded Apply.

## SOURCE-CLOSURE CORRECTIONS

The final source review closed the integration issues found before the compile gate:

- fixed invalid `Vector<String>::Find()` use in the track artwork queue;
- preserved Working `UiList` internal reorder capture in `PlaylistTransferList`;
- preserved Ctrl/Shift multi-selection when starting Spotify/Imported transfer drag;
- kept empty Working enabled as a transfer drop target;
- wired Enter in Imported Find and used the explicit UTF-8 editor text API;
- removed the U++ `Vector<String>` by-value capture from the create-playlist worker;
- bounded track-artwork queue attempts;
- bounded failed artwork fetches at `SpotifyImageCache` with a transparent in-memory marker so a bad URL is not requeued indefinitely during the session while a restart may retry it;
- refreshed target accessibility/artwork state after verified Apply readback;
- checked split provider declarations/definitions and package membership, including `SpotifyClientSearch.cpp`, `SpotifyClientReplace.cpp`, and `PlaylistTransferList.h`;
- retained exact guarded Apply authority and did not weaken deterministic planner behavior.

Final source-closure commit: `c205094486097f1aecb7067781d304677e8e7b8e`.

## UI DEPENDENCY

Exact `upp_Ui/main` dependency accepted for this PlaylistLab validation:

`91f2926f57a86dfc6df08d9b0ae10173085dcbf5`

The earlier UiList/UiTable decoration repair remains in that ancestry: cumulative drag/badge lanes, authored gaps, single badge-text ownership, composed drag/check/icon/metadata geometry, rounded inner-viewport clipping, and the subsequent Draw::Begin/End fix.

## VERIFIED WINDOWS VALIDATION

Gary validated the exact pair:

- PlaylistLab: `1989b0e9ff9e92d8c52a0b283d0706eedb11bc9a`
- upp_Ui: `91f2926f57a86dfc6df08d9b0ae10173085dcbf5`

Results:

- `UiListStyleContractTest`: PASS — `checks=15 failed=0`
- `UiModelViewPerformanceTest`: PASS — `Checks: 52, Fails: 0`
- relevant Ui sources/test packages compiled successfully with no diagnostics
- `PlaylistCoreTest`: PASS — `88 checks completed`, 0 failures
- PlaylistLab CLANGx64 `GUI MT`: compile PASS, link PASS, no diagnostics
- `PlaylistLab.upp` package membership checked for `PlaylistTransferList.h`, `SpotifyClientSearch.cpp`, and `SpotifyClientReplace.cpp`
- Spotify declarations/definitions plus Create/Apply/Replace references checked
- `git diff --check`: PASS
- validation worktree: clean
- no temporary/unrelated tracked files
- no fixups were required
- no Spotify mutation was performed

Final validated executable path:

`E:\apps\github\upp_playlistlab\build\PlaylistLab.exe`

## BRANCH STATE

`Trilec/upp_playlistlab` has only `main`. No stale PlaylistLab feature branches remain.

## VALIDATION BOUNDARY

Windows compile/test acceptance is VERIFIED for the exact SHAs above. The current code has not yet received a complete human-visible interaction acceptance for every newly added workflow, and Create/Apply/Replace have not yet been exercised against a disposable live Spotify target in this closure slice.

Do not turn successful compile/test evidence into a claim that live Spotify mutation paths are already acceptance-tested.

## BUILD OUTPUT CONVENTION

Intermediate U++ objects may live below `build`, but the final executable for Curt remains:

`E:\apps\github\upp_playlistlab\build\PlaylistLab.exe`

## NEXT ACTION

Curt performs the human-visible GUI review of the validated executable, focusing on:

- typed Find -> Imported staging;
- track artwork in Spotify / Imported / Working rows;
- Spotify -> Working and Imported -> Working drag/drop, including multi-selection;
- Working internal reorder with right-side grip;
- tooltip/help clarity for Resolve, placement/order mode, and destination actions;
- Create New Playlist / Apply to Selected Playlist / Replace Selected Playlist visibility and confirmation flow;
- shared/read-only Spotify playlist behavior where the Web API permits or refuses item access.

After that visual pass, if desired, choose a clearly disposable Spotify playlist/account context and run bounded live mutation acceptance in this order: Create New first, guarded Apply second, explicit Replace last. Do not use an important playlist for initial mutation validation.
