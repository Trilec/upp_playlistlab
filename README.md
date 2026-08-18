# upp_playlistlab

U++ playlist/set-list workbench for turning pasted text, CSV files, and Spotify playlists into reviewable ordered playlists.

## PlaylistWorkbench

`PlaylistWorkbench/` is the main GUI package. The first release is designed around four workflows:

1. import a CSV or paste a plain-text set list;
2. resolve requested songs against Spotify while keeping ambiguous matches visible;
3. load an owned/collaborative Spotify playlist and compare/reorder it against the working list;
4. create a new Spotify playlist, or safely update/reorder an existing one after previewing the plan.

The workbench uses Spotify's current Web API `/items` playlist endpoints and Authorization Code with PKCE. Spotify Desktop does not need to be running; changes are made to the account and appear in normal Spotify clients.

## Repository layout

- `PlaylistWorkbench/` — U++ GUI application and playlist/Spotify implementation.
- `tests/PlaylistCoreTest/` — deterministic non-network core tests.
- `examples/` — sample import files.
- `docs/` — design notes, setup instructions and `ACTIVE_WORK.md` recovery state.

## Dependencies

- U++ Core/CtrlLib
- U++ `Ui` package from `Trilec/upp_Ui` for the modern demo-style shell
- Spotify Premium/developer application for live Spotify operations

See `docs/SPOTIFY_SETUP.md` before connecting an account.
