# upp_playlistlab

U++ playlist/set-list application for turning pasted text, CSV files, Spotify search results, and Spotify playlists into a local ordered Working Playlist.

## PlaylistLab

`PlaylistLab/` is the main GUI package. The current workspace deliberately has two playlist surfaces:

1. **Selected Spotify Playlist** — provider/source context. Readability is verified by the actual Spotify item request and is tracked separately from editability.
2. **Working Playlist** — the local authored document and the only publish-authoritative list.

Tracks can be copied or dragged from a readable Spotify playlist into Working. Spotify Find, CSV import, and pasted text add directly to Working rather than creating a third staging playlist. Imported text remains unresolved until the user explicitly chooses **Resolve**; ambiguous candidates stay visible for manual review before they become publishable.

Working supports local multi-selection, removal, export, right-grip drag reorder, an editable playlist name, locally persisted per-track notes, artwork, and a contextual track inspector. Spotify Client IDs can be stored as friendly named profiles; PlaylistLab never asks for or stores a Client Secret.

## Spotify destination actions

Spotify writes are separate explicit operations, each preceded by an exact preview. The preview itself is the confirmation dialog; cancelling it performs no write.

- **Create New** — creates a new private Spotify playlist from the exact publishable Working order.
- **Append Missing** — occurrence-aware append only. It adds Working occurrences not already represented in the selected editable playlist, at the end, without reordering or deleting existing items.
- **Replace** — explicitly destructive. It makes the selected editable playlist match the exact Working sequence.

Append and Replace require current snapshot evidence, re-read the Spotify target, surface stale/partial outcomes, and verify the final remote sequence before reporting success. No mutation path silently retries, replans, deletes, or rolls back behind the user's preview.

The older deterministic add/reorder planner remains core-tested infrastructure, but it is no longer the primary simplified workspace action.

## Repository layout

- `PlaylistLab/` — U++ GUI application and playlist/Spotify implementation.
- `tests/PlaylistCoreTest/` — deterministic non-network core tests.
- `examples/` — sample import files.
- `docs/` — design notes, Spotify setup, and `ACTIVE_WORK.md` recovery state.

## Dependencies

- U++ Core/CtrlLib
- U++ `Ui` package from `Trilec/upp_Ui`
- Spotify developer application for live Spotify workflows

See `docs/SPOTIFY_SETUP.md` before connecting an account.
