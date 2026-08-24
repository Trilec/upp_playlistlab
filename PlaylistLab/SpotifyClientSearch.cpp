#include "SpotifyClient.h"

namespace Upp {
namespace {

ValueMap SearchMap(const ValueMap& map, const char *key)
{
    Value value = map[key];
    return IsValueMap(value) ? ValueMap(value) : ValueMap();
}

ValueArray SearchArray(const ValueMap& map, const char *key)
{
    Value value = map[key];
    return IsValueArray(value) ? ValueArray(value) : ValueArray();
}

} // namespace

bool SpotifyClient::SearchTracks(const String& query_text, Vector<SpotifyTrack>& tracks, int limit)
{
    tracks.Clear();
    String query = TrimBoth(query_text);
    if(query.IsEmpty()) {
        last_status = 0;
        last_error = "Type a song title, artist, or both before searching Spotify.";
        return false;
    }

    limit = minmax(limit, 1, 10);
    Value response;
    String path = "/search?type=track&limit=" + AsString(limit) + "&q=" + UrlEncode(query);
    if(!RequestJson("GET", path, String(), response) || !IsValueMap(response))
        return false;

    ValueMap root = response;
    ValueMap page = SearchMap(root, "tracks");
    ValueArray items = SearchArray(page, "items");
    for(int i = 0; i < items.GetCount(); ++i) {
        SpotifyTrack track;
        if(JsonTrack(items[i], track))
            tracks.Add(pick(track));
    }
    return true;
}

} // namespace Upp
