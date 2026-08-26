#include "SpotifyClient.h"

namespace Upp {

bool SpotifyClient::UpdatePlaylistDetails(const String& playlist_id, const String& name)
{
    String trimmed = TrimBoth(name);
    if(playlist_id.IsEmpty()) {
        last_status = 0;
        last_error = "Spotify playlist rename has no target playlist.";
        return false;
    }
    if(trimmed.IsEmpty()) {
        last_status = 0;
        last_error = "Spotify playlist name cannot be empty.";
        return false;
    }

    Json body;
    body("name", trimmed);
    Value response;
    return RequestJson("PUT", "/playlists/" + UrlEncode(playlist_id), ~body, response);
}

} // namespace Upp
