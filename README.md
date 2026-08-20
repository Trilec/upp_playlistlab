# upp_playlistlab

U++ playlist/set-list application for turning pasted text, CSV files, and Spotify playlists into reviewable ordered playlists.

## PlaylistLab

`PlaylistLab/` is the main GUI package. The application is designed around four workflows:

1. import a CSV or paste a plain-text set list;
2. resolve requested songs against Spotify while keeping ambiguous matches visible until explicitly confirmed;
3. load an owned/collaborative Spotify playlist and compare/reorder it against the local working list;
4. inspect a deterministic change preview, then explicitly publish that exact plan to Spotify.

The local `PlaylistDocument` is the authored state. Spotify is a provider through the Web API rather than a Spotify Desktop automation layer, so imported lists can be edited and tested without a network connection.

Spotify mutation is exposed only from a publishable preview and requires a second explicit confirmation. The target is re-read and snapshot-checked before mutation; planned additions are appended without deleting unrelated items, the augmented target is verified before any reorder, and the final target is read back and compared with the exact preview. Stale or partially completed operations are surfaced rather than silently replanned.

## Repository layout

- `PlaylistLab/` — U++ GUI application and playlist/Spotify implementation.
- `tests/PlaylistCoreTest/` — deterministic non-network core tests.
- `examples/` — sample import files.
- `docs/` — design notes, setup instructions and `ACTIVE_WORK.md` recovery state.

## Dependencies

- U++ Core/CtrlLib
- U++ `Ui` package from `Trilec/upp_Ui` for the model-backed application shell
- Spotify developer application for live Spotify workflows

See `docs/SPOTIFY_SETUP.md` before connecting an account.
