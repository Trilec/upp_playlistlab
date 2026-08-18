# Spotify setup

PlaylistLab uses Spotify Web API authorization with **Authorization Code + PKCE**. It does not require a client secret and does not automate the Spotify desktop application.

## Developer application

1. Create a Spotify developer application and copy its **Client ID**.
2. Add this exact redirect URI to the application's redirect allowlist:

   `http://127.0.0.1:43821/callback`

3. PlaylistLab opens the normal Spotify authorization page in the system browser and receives the callback on the loopback interface.

The application requests the playlist read/modify scopes needed for owned or collaborative playlist workflows plus `user-read-private` for current-user identity.

## Local token state

PlaylistLab stores the Client ID and refresh token in its normal U++ config file (`playlistlab.spotify.json`). The access token is kept only in memory. If Spotify rejects a refresh token with `invalid_grant`, PlaylistLab clears it and requires normal authorization again rather than repeatedly retrying the expired/revoked credential.

## Current API contract

PlaylistLab targets the current playlist-item API:

- `GET /me/playlists`
- `GET /playlists/{id}/items`
- `POST /playlists/{id}/items`
- `PUT /playlists/{id}/items`
- `POST /me/playlists`

Search requests are capped at 10 results to match Spotify's current Web API limit.

## Safety rules

- Ambiguous search candidates remain `Review` and are not publishable until explicitly confirmed.
- Playlist item positions are retained even when Spotify returns an unavailable/non-track item, so subsequent reorder indices remain correct.
- Synthetic unavailable-item identifiers are for local ordering only and must never be submitted as new Spotify playlist items.
- Reorder writes carry the latest playlist `snapshot_id` when available.
