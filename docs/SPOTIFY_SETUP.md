# Spotify setup

PlaylistLab uses Spotify Web API authorization with **Authorization Code + PKCE**. It does not require a client secret and does not automate the Spotify desktop application.

The application also contains a **Help** dialog (`?` in the top-right header) with the same setup flow, direct links to Spotify's dashboard/documentation, and a button that copies the required redirect URI.

## Before you start

For Spotify **Development Mode**, the app owner currently needs an active **Spotify Premium** subscription. Spotify changed Development Mode and parts of the Web API during 2026, so old tutorials can describe endpoints or account assumptions that no longer apply.

PlaylistLab cannot discover a Developer Client ID from the normal Spotify client or account-apps page. The user must create/open their app in the Spotify Developer Dashboard and copy its Client ID.

## Developer application

1. Sign in to Spotify in the normal web browser first.
2. Open the Spotify Developer Dashboard: `https://developer.spotify.com/dashboard`.
3. Create a developer application, or open the existing PlaylistLab application.
4. Copy its **Client ID** from the app's Basic Information page.
5. Add this exact redirect URI to the application's redirect allowlist:

   `http://127.0.0.1:43821/callback`

6. In PlaylistLab, use `+` beside the Client profile selector, enter a friendly profile name and paste the Client ID.
7. Refresh/authorize. PlaylistLab opens Spotify's normal authorization page and receives the callback on the loopback interface.

The Spotify Account Apps page (`https://www.spotify.com/nz/account/apps/`) is useful for reviewing connected applications, but it is **not** the Developer Dashboard page that exposes the Client ID.

PlaylistLab never requests or stores a Client Secret.

## Authorization lifetime

The **Client ID itself does not expire**.

Spotify now gives user refresh tokens a **six-month lifetime (about 180 days)**. Refreshing an access token does not extend that six-month lifetime. When the refresh token expires (or is revoked), PlaylistLab requires normal browser authorization again; the existing app and Client ID can continue to be used.

PlaylistLab stores the selected Client ID profile and refresh token in its normal U++ config state. The access token is kept only in memory. If Spotify rejects a refresh token with `invalid_grant`, PlaylistLab clears it and requires authorization again rather than repeatedly retrying the expired/revoked credential.

## Current API contract

PlaylistLab targets the current playlist-item API, including:

- `GET /me/playlists`
- `GET /playlists/{id}/items`
- `POST /playlists/{id}/items`
- `PUT /playlists/{id}/items`
- `POST /me/playlists`

Search requests are capped at 10 results to match Spotify's current Web API limit.

Spotify metadata visibility, item readability, and editability are not treated as the same thing. A followed playlist can appear in the library even when Spotify's third-party item endpoint later refuses its track list. PlaylistLab records actual item-access evidence as `CHECK`, `SOURCE`, or `META` instead of assuming readability from ownership metadata.

## PlaylistLab workflow

- **Selected Spotify Playlist** is the Spotify source/current target context.
- **Working Playlist** is the local authored list and the only publish-authoritative document.
- Spotify tracks, Find results, CSV imports, and pasted text feed Working.
- **Resolve** searches Spotify for unresolved imported/pasted rows; ambiguous matches remain review state.
- **Create New** creates a new private playlist in exact Working order.
- **Append Missing** appends only missing Working occurrences, with no reorder or delete.
- **Replace** is explicitly destructive and makes the selected editable playlist match Working exactly.

Every Spotify write shows an exact preview first. Cancelling the preview performs no write.

## Safety rules

- Ambiguous search candidates remain `Review` and are not publishable until explicitly confirmed.
- Playlist item positions are retained even when Spotify returns an unavailable/non-track item, so subsequent ordering indices remain correct.
- Synthetic unavailable-item identifiers are for local ordering only and must never be submitted as new Spotify playlist items.
- Writes use current playlist snapshot evidence where available, surface stale/partial outcomes, and verify final remote state before reporting success.
- PlaylistLab-owned track notes are local metadata and are never sent to Spotify.
