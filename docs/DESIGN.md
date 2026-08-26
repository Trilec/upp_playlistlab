# PlaylistLab design

## Product boundary

PlaylistLab is an ordered-playlist workbench, not a Spotify Desktop automation layer. Spotify is a provider through the Web API. The local `PlaylistDocument` remains authored state so Working can be edited, saved, imported, annotated, reordered, and core-tested independently of live Spotify mutations.

## Current workspace model

The visible product model is intentionally small:

`Spotify source/context -> Working Playlist -> explicit Spotify destination action`

There is no separate visible Imported Playlist. CSV, clipboard text, and Spotify Find add directly to Working. Imported title/artist rows remain unresolved until explicit **Resolve**; ambiguous matches remain `TRACK_REVIEW` until the user confirms a candidate. Only `TRACK_EXACT` and `TRACK_AUTO` expose publishable Spotify identity.

A selected Spotify playlist remains separate provider context. Its metadata may be visible even when Spotify's current playlist-items API refuses item access. PlaylistLab therefore tracks read evidence separately from editability: an untested noneditable playlist is `CHECK`, successful read-only access becomes `SOURCE`, a real 403/404 becomes `META`, and an editable target remains `EDIT`.

## Local Working authority

Working is the only publish-authoritative list. It owns:

- exact local order;
- duplicate occurrences;
- requested/resolved track identity and candidate evidence;
- editable playlist name;
- locally persisted per-track `user_note` annotations;
- source label and export state.

Working reorder is request-first: `UiList` internal mutation is disabled, the document is moved first, then the projection is rebuilt. This prevents view order becoming a second source of truth.

## Spotify destination semantics

Every Spotify write is explicit and previewed.

### Create New

Creates a new private Spotify playlist and populates it from the exact publishable Working order. A creation that succeeds but only partially populates is reported as partial evidence rather than treated as atomic success.

### Append Missing

Append Missing is intentionally not the old reorder planner under a new label. The pure helper `BuildAppendMissingUris()` compares occurrence counts, preserves Working order for missing occurrences, and returns only the items that must be appended. Execution then:

1. stable-reads the current target and snapshot;
2. rejects stale snapshot evidence;
3. computes the exact missing occurrence sequence;
4. appends only that sequence;
5. stable-reads again;
6. succeeds only if the final target equals the original target plus those appended occurrences.

It never reorders or deletes existing target items.

### Replace

Replace is separately destructive. It requires an editable loaded target, exact Working URIs, preview confirmation, current snapshot preflight, and exact final readback. Items absent from Working may be removed because that is the explicit meaning of Replace.

## Legacy planner infrastructure

The deterministic reference-slot/reference-first planner remains in core code and tests because it is useful verified infrastructure and supports guarded historical Apply logic. It is no longer exposed as the main simplified workspace workflow and should not be used to reintroduce a third list or placement-mode UI without a new product decision.

## UI direction

The dark `upp_Ui` workspace uses four visual regions:

- compact Spotify playlist/profile library;
- selected Spotify playlist and track source;
- Working Playlist editing surface;
- contextual track inspector.

The inspector shows artwork, title/artist/album/time, match state, contextual Review Match, Play in Spotify, and PlaylistLab-owned notes. Source and destination split-buttons remember their selected default actions. The positive preview action has a restrained green pulse; Cancel exits without mutation.

## Safety rules

- no Spotify write without an exact preview;
- preview cancellation performs no write;
- snapshot-aware stale-target rejection;
- final remote readback before success where mutation occurs;
- failed mutation requests are treated conservatively as potentially partial;
- no silent retry, replan, delete, or rollback;
- exact Spotify identity is state-gated;
- duplicate occurrences remain explicit;
- network/provider behavior stays outside deterministic planner tests where possible.
