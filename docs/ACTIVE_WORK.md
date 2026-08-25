# ACTIVE WORK

SOURCE BASE: `c205094486097f1aecb7067781d304677e8e7b8e` (`main` source-closure checkpoint)
TASK: PL-001 — final Windows compile/test acceptance of the completed PlaylistLab workspace and guarded Spotify destination workflows
STATUS: SOURCE CLOSURE COMPLETE; WINDOWS VALIDATION PENDING

## CURRENT PRODUCT STATE

PlaylistLab now has three separate playlist concepts and keeps their ownership explicit:

1. **Spotify Playlist** — selected Spotify target/source context and its readable items.
2. **Imported Playlist** — CSV, clipboard, or typed Spotify Find staging with match/review/missing evidence.
3. **Working Playlist** — the only editable/publish-authoritative document used for local reorder, Apply, Create New Playlist, and explicit Replace.

Spotify and Imported lists support Ctrl/Shift multi-selection, Add Selected/Add All, artwork, and copy drag/drop into Working. Working remains the semantic authority for reorder; `UiList` internal mutation is disabled and reorder is applied to `PlaylistDocument` before rebuilding the projection. Empty Working remains an active drop target. The Working drag grip is on the right and multi-selection is preserved when a selected source row begins a transfer drag.

Imported Playlist supports Import CSV, Paste Text, Export CSV, Resolve, Review Match, Remove, Clear, and typed Spotify **Find**. Enter in the Find editor invokes the same Spotify search as the Find button. Search results are staged in Imported and do not become publish authority until explicitly copied into Working.

## CLIENT / LOCAL STATE

PlaylistLab stores friendly-name + Spotify Client ID profiles in `playlistlab.clients.json`; no Client Secret is requested or stored. `SpotifyAuth` remains token authority for the selected Client ID. Changing Client ID clears tokens belonging to the previous client.

Working state and source label persist in `playlistlab.working.json`; UI target/order state persists in `playlistlab.ui.json`; Spotify auth state persists in `playlistlab.spotify.json`; artwork is cached under `playlistlab-cache`. Working state preserves candidates and match state so unresolved/review/missing evidence survives restart.

## SPOTIFY DESTINATIONS AND SAFETY

Only `TRACK_EXACT` / `TRACK_AUTO` expose publishable Spotify identity. Review/Missing/Unresolved rows remain blocked.

**Apply to Selected Playlist** retains the guarded non-destructive publisher. It executes the exact stored preview after a fresh target/snapshot read, appends only missing occurrences, preserves unrelated/unavailable positions, performs the exact stored reorder plan, uses evolving snapshots, treats failed mutations conservatively, and succeeds only after exact final readback. Apply never deletes or replaces target items.

**Create New Playlist** creates a private Spotify playlist and populates it from the exact publishable Working order. The worker now owns its URI sequence through member-held pending state rather than capturing a U++ `Vector` by value. If creation succeeds but population stops, PlaylistLab reports that partial state and reloads playlist metadata rather than pretending the operation was atomic.

**Replace Selected Playlist** is a separate explicitly destructive path. It requires an editable loaded target, exact before/after preview, destructive warning, explicit second confirmation, fresh target/snapshot preflight, exact desired Working order, and exact final readback. It is not silently folded into Apply. `SpotifyClient::ExecuteReplaceItems` replaces/clears the first 100 URIs with `PUT /playlists/{id}/items`, appends any tail in guarded batches, recovers observed state when possible, and reports stale/partial/observed evidence.

No delete/replace/rollback behavior exists inside guarded Apply.

## SOURCE-CLOSURE CORRECTIONS

The final source review closed the integration issues found before the compile gate:

- fixed the invalid `Vector<String>::Find()` lookup in the track artwork queue;
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

PlaylistLab depends on `Trilec/upp_Ui` current validated-source line. Exact dependency HEAD frozen for this compile gate:

`upp_Ui/main`: `91f2926f57a86dfc6df08d9b0ae10173085dcbf5`

The earlier shared UiList/UiTable decoration repair remains part of that ancestry: cumulative drag/badge lanes, authored gaps, single badge-text ownership, composed drag/check/icon/metadata geometry, and rounded inner-viewport clipping. Gary must compile/test the exact frozen Ui HEAD above before PlaylistLab acceptance is called current.

## BRANCH STATE

`Trilec/upp_playlistlab` has only `main`. No stale PlaylistLab feature branches remain to clean up.

## VALIDATION STATE

Historical guarded-publish baseline remains VERIFIED: `PlaylistCoreTest` passed 88 checks and PlaylistLab previously built/linked cleanly under Windows U++/CLANGx64 `GUI MT` at the accepted guarded checkpoint.

Current three-list UI, transfer adapter, Spotify Find, artwork integration, Create New Playlist, explicit Replace backend/UI, and final source corrections are **NOT YET Windows compiled/tested**. No live Spotify mutation has been performed for this closure slice. Do not convert source review into a build/runtime claim.

## BUILD OUTPUT CONVENTION

Gary may place intermediate U++ objects under subdirectories of `build`, but the final executable for Curt must be emitted exactly as:

`E:\apps\github\upp_playlistlab\build\PlaylistLab.exe`

## NEXT ACTION — GARY COMPILE GATE

Gary must:

1. Fetch `Trilec/upp_Ui/main` and verify exact HEAD `91f2926f57a86dfc6df08d9b0ae10173085dcbf5` before testing the dependency.
2. Run the relevant Ui control validation, especially `Utilities/UiListStyleContractTest`, current List/Table model-view coverage, and compile the relevant List/Table demos or packages needed to prove the frozen Ui line builds.
3. Fetch `Trilec/upp_playlistlab/main` and verify it matches the exact checkpoint SHA supplied by the supervisor after this ACTIVE_WORK commit.
4. Run `tests/PlaylistCoreTest` under Windows U++/CLANGx64.
5. Build and link PlaylistLab under CLANGx64 `GUI MT` against the frozen Ui dependency.
6. Emit the final executable directly as `E:\apps\github\upp_playlistlab\build\PlaylistLab.exe`.
7. Report exact PlaylistLab and `upp_Ui` SHAs, test counts/results, compiler/linker diagnostics, and final executable path.
8. Gary may fix only a tiny obvious compile/API mismatch if encountered; any fix must be committed to `main` and reported with the exact SHA and diff. Anything architectural or ambiguous comes back to the supervisor.
9. Do **not** perform a live Spotify Create/Apply/Replace mutation unless Curt explicitly chooses a disposable playlist after visual review.

After Gary's compile gate passes, Curt performs the human-visible GUI smoke/interaction review and may then choose whether to run a disposable live Spotify mutation test.
