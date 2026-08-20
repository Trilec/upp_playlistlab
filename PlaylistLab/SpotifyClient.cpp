#include "SpotifyClient.h"

namespace Upp {
namespace {

String VString(const ValueMap& map, const char *key)
{
    Value v = map[key];
    return IsNull(v) || v.IsVoid() ? String() : AsString(v);
}

int VInt(const ValueMap& map, const char *key, int fallback = 0)
{
    Value v = map[key];
    return IsNull(v) || v.IsVoid() ? fallback : (int)v;
}

bool VBool(const ValueMap& map, const char *key, bool fallback = false)
{
    Value v = map[key];
    return IsNull(v) || v.IsVoid() ? fallback : (bool)v;
}

ValueMap VMap(const ValueMap& map, const char *key)
{
    Value v = map[key];
    return IsValueMap(v) ? ValueMap(v) : ValueMap();
}

ValueArray VArray(const ValueMap& map, const char *key)
{
    Value v = map[key];
    return IsValueArray(v) ? ValueArray(v) : ValueArray();
}

String JoinArtistNames(const ValueArray& artists)
{
    String out;
    for(int i = 0; i < artists.GetCount(); i++) {
        if(!IsValueMap(artists[i]))
            continue;
        String name = VString(ValueMap(artists[i]), "name");
        if(name.IsEmpty())
            continue;
        if(!out.IsEmpty())
            out << ", ";
        out << name;
    }
    return out;
}

String JsonBodyForUris(const Vector<String>& uris, int from, int count)
{
    JsonArray array;
    for(int i = from; i < from + count; i++)
        array << uris[i];
    Json json;
    json("uris", array);
    return ~json;
}

Vector<String> TrackUris(const Vector<SpotifyTrack>& tracks)
{
    Vector<String> uris;
    uris.Reserve(tracks.GetCount());
    for(const SpotifyTrack& track : tracks)
        uris.Add(track.uri);
    return uris;
}

} // namespace

SpotifyClient::SpotifyClient(SpotifyAuth& auth_)
    : auth(auth_)
{
}

String SpotifyClient::ApiUrl(const String& path) const
{
    return path.StartsWith("http://") || path.StartsWith("https://")
        ? path : "https://api.spotify.com/v1" + path;
}

void SpotifyClient::SetApiError(int status, const String& body)
{
    last_status = status;
    last_error = Format("Spotify API returned HTTP %d", status);
    try {
        Value parsed = ParseJSON(body);
        if(IsValueMap(parsed)) {
            ValueMap root = parsed;
            Value error = root["error"];
            if(IsValueMap(error)) {
                ValueMap e = error;
                String message = VString(e, "message");
                String reason = VString(e, "reason");
                if(!message.IsEmpty()) last_error << ": " << message;
                if(!reason.IsEmpty()) last_error << " (" << reason << ')';
            }
            else {
                String message = VString(root, "message");
                String reason = VString(root, "reason");
                if(!message.IsEmpty()) last_error << ": " << message;
                if(!reason.IsEmpty()) last_error << " (" << reason << ')';
            }
        }
    }
    catch(...) {}
}

bool SpotifyClient::Request(const String& method, const String& url_or_path, const String& body,
                            String& response, bool allow_refresh)
{
    last_error.Clear();
    last_status = 0;
    if(!auth.EnsureAccessToken()) {
        last_error = auth.GetLastError();
        return false;
    }

    for(int attempt = 0; attempt < 3; attempt++) {
        HttpRequest request(ApiUrl(url_or_path));
        request.Authorization("Bearer " + auth.GetAccessToken())
               .Accept("application/json")
               .RequestTimeout(30000);

        if(method == "POST") request.POST();
        else if(method == "PUT") request.PUT();
        else if(method == "DELETE") request.DEL();
        else request.GET();

        if(!body.IsEmpty())
            request.ContentType("application/json").PostData(body);

        response = request.Execute();
        last_status = request.GetStatusCode();

        if(request.IsSocketError()) {
            last_error = "Spotify network request failed: " + request.GetErrorDesc();
            return false;
        }

        if(last_status == 401 && allow_refresh) {
            auth.InvalidateAccessToken();
            if(!auth.RefreshAccessToken()) {
                last_error = auth.GetLastError();
                return false;
            }
            allow_refresh = false;
            continue;
        }

        if(last_status == 429) {
            int seconds = StrInt(request.GetHeader("Retry-After"));
            if(seconds > 0 && seconds <= 5 && attempt < 2) {
                Sleep(seconds * 1000);
                continue;
            }
            last_error = seconds > 0
                       ? Format("Spotify rate limit reached; retry after %d second(s).", seconds)
                       : "Spotify rate limit reached; retry later.";
            return false;
        }

        if(last_status < 200 || last_status >= 300) {
            SetApiError(last_status, response);
            return false;
        }
        return true;
    }

    last_error = "Spotify request retry limit reached.";
    return false;
}

bool SpotifyClient::RequestJson(const String& method, const String& url_or_path, const String& body,
                                Value& response)
{
    String text;
    if(!Request(method, url_or_path, body, text))
        return false;
    if(text.IsEmpty()) {
        response = ValueMap();
        return true;
    }
    try {
        response = ParseJSON(text);
        return true;
    }
    catch(...) {
        last_error = "Spotify returned invalid JSON.";
        return false;
    }
}

bool SpotifyClient::JsonTrack(const Value& value, SpotifyTrack& track) const
{
    if(!IsValueMap(value))
        return false;
    ValueMap map = value;
    String type = VString(map, "type");
    if(type.IsEmpty())
        type = "track";
    if(type != "track" && type != "episode")
        return false;

    track.type = type;
    track.id = VString(map, "id");
    track.uri = VString(map, "uri");
    track.title = VString(map, "name");
    track.duration_ms = VInt(map, "duration_ms");
    if(type == "track") {
        track.artist = JoinArtistNames(VArray(map, "artists"));
        track.album = VString(VMap(map, "album"), "name");
        track.isrc = VString(VMap(map, "external_ids"), "isrc");
    }
    else {
        ValueMap show = VMap(map, "show");
        track.artist = VString(show, "name");
        track.album = "Podcast episode";
    }
    return !track.uri.IsEmpty();
}

bool SpotifyClient::GetCurrentUser(String& user_id, String& display_name)
{
    Value response;
    if(!RequestJson("GET", "/me", String(), response) || !IsValueMap(response))
        return false;
    ValueMap user = response;
    user_id = VString(user, "id");
    display_name = VString(user, "display_name");
    if(display_name.IsEmpty())
        display_name = user_id;
    return !user_id.IsEmpty();
}

bool SpotifyClient::GetEditablePlaylists(Vector<SpotifyPlaylistInfo>& playlists)
{
    playlists.Clear();
    String user_id, display_name;
    if(!GetCurrentUser(user_id, display_name))
        return false;

    String next = "/me/playlists?limit=50&offset=0";
    while(!next.IsEmpty()) {
        Value response;
        if(!RequestJson("GET", next, String(), response) || !IsValueMap(response))
            return false;
        ValueMap page = response;
        ValueArray items = VArray(page, "items");
        for(int i = 0; i < items.GetCount(); i++) {
            if(!IsValueMap(items[i]))
                continue;
            ValueMap item = items[i];
            ValueMap owner = VMap(item, "owner");
            bool collaborative = VBool(item, "collaborative");
            String owner_id = VString(owner, "id");
            if(owner_id != user_id && !collaborative)
                continue;

            SpotifyPlaylistInfo& info = playlists.Add();
            info.id = VString(item, "id");
            info.uri = VString(item, "uri");
            info.name = VString(item, "name");
            info.owner_id = owner_id;
            info.owner_name = VString(owner, "display_name");
            info.snapshot_id = VString(item, "snapshot_id");
            info.item_count = VInt(VMap(item, "items"), "total", VInt(VMap(item, "tracks"), "total"));
            info.collaborative = collaborative;
            info.is_public = VBool(item, "public");
        }
        next = VString(page, "next");
    }
    return true;
}

bool SpotifyClient::GetPlaylistItems(const String& playlist_id, Vector<SpotifyTrack>& tracks, String *snapshot_id)
{
    tracks.Clear();
    String snapshot_before;
    if(snapshot_id) {
        Value meta;
        if(!RequestJson("GET", "/playlists/" + UrlEncode(playlist_id) + "?fields=snapshot_id", String(), meta) ||
           !IsValueMap(meta))
            return false;
        snapshot_before = VString(ValueMap(meta), "snapshot_id");
    }

    String next = "/playlists/" + UrlEncode(playlist_id) + "/items?limit=50&offset=0&additional_types=episode";
    while(!next.IsEmpty()) {
        Value response;
        if(!RequestJson("GET", next, String(), response) || !IsValueMap(response))
            return false;
        ValueMap page = response;
        ValueArray items = VArray(page, "items");
        for(int i = 0; i < items.GetCount(); i++) {
            if(!IsValueMap(items[i]))
                continue;
            ValueMap wrapper = items[i];
            Value value = wrapper["item"];
            if(value.IsVoid() || IsNull(value))
                value = wrapper["track"];

            SpotifyTrack track;
            if(JsonTrack(value, track)) {
                tracks.Add(pick(track));
                continue;
            }

            track.type = "unavailable";
            track.placeholder = true;
            if(IsValueMap(value))
                track.uri = VString(ValueMap(value), "uri");
            if(track.uri.IsEmpty())
                track.uri = "playlistlab:unavailable:" + AsString(tracks.GetCount());
            track.title = "[Unavailable playlist item]";
            track.artist = "Position preserved for Spotify reordering";
            tracks.Add(pick(track));
        }
        next = VString(page, "next");
    }

    if(snapshot_id) {
        Value meta;
        if(!RequestJson("GET", "/playlists/" + UrlEncode(playlist_id) + "?fields=snapshot_id", String(), meta) ||
           !IsValueMap(meta))
            return false;
        String snapshot_after = VString(ValueMap(meta), "snapshot_id");
        if(!snapshot_before.IsEmpty() && !snapshot_after.IsEmpty() && snapshot_before != snapshot_after) {
            last_status = 0;
            last_error = "Spotify playlist changed while PlaylistLab was reading it; reload and try again.";
            return false;
        }
        *snapshot_id = snapshot_after.IsEmpty() ? snapshot_before : snapshot_after;
    }
    return true;
}

bool SpotifyClient::SearchTracks(const TrackEntry& request, Vector<SpotifyTrack>& tracks, int limit)
{
    tracks.Clear();
    limit = minmax(limit, 1, 10);

    String query;
    if(!request.requested_title.IsEmpty())
        query << "track:" << request.requested_title;
    if(!request.requested_artist.IsEmpty()) {
        if(!query.IsEmpty()) query << ' ';
        query << "artist:" << request.requested_artist;
    }
    if(query.IsEmpty())
        query = request.requested_title;
    if(query.IsEmpty()) {
        last_error = "Cannot search Spotify without a title or artist.";
        return false;
    }

    Value response;
    String path = "/search?type=track&limit=" + AsString(limit) + "&q=" + UrlEncode(query);
    if(!RequestJson("GET", path, String(), response) || !IsValueMap(response))
        return false;

    ValueMap root = response;
    ValueMap page = VMap(root, "tracks");
    ValueArray items = VArray(page, "items");
    for(int i = 0; i < items.GetCount(); i++) {
        SpotifyTrack track;
        if(JsonTrack(items[i], track))
            tracks.Add(pick(track));
    }
    return true;
}

bool SpotifyClient::ResolveTrack(TrackEntry& entry)
{
    if(!entry.spotify_uri.IsEmpty() && entry.state != TRACK_REVIEW) {
        entry.state = TRACK_EXACT;
        entry.confidence = 100;
        return true;
    }

    Vector<SpotifyTrack> found;
    if(!SearchTracks(entry, found, 10))
        return false;

    entry.candidates = pick(found);
    entry.selected_candidate = -1;
    entry.spotify_uri.Clear();
    entry.confidence = 0;
    if(entry.candidates.IsEmpty()) {
        entry.state = TRACK_MISSING;
        entry.note = "No Spotify candidates found";
        return true;
    }

    int best = -1, best_score = -1, second_score = -1;
    for(int i = 0; i < entry.candidates.GetCount(); i++) {
        int score = ScoreTrackCandidate(entry, entry.candidates[i]);
        if(score > best_score) {
            second_score = best_score;
            best_score = score;
            best = i;
        }
        else if(score > second_score)
            second_score = score;
    }

    entry.confidence = max(0, best_score);
    if(best_score >= 92 && (second_score < 0 || best_score - second_score >= 8)) {
        entry.SelectCandidate(best, TRACK_AUTO);
        entry.confidence = best_score;
        entry.note = "Auto-matched by title/artist";
    }
    else {
        entry.selected_candidate = best;
        entry.state = TRACK_REVIEW;
        entry.note = best_score > 0 ? Format("Best candidate score %d", best_score)
                                    : "Candidate requires manual review";
    }
    return true;
}

bool SpotifyClient::ResolveDocument(PlaylistDocument& document, Gate<int, int> progress)
{
    for(int i = 0; i < document.tracks.GetCount(); i++) {
        if(progress && !progress(i, document.tracks.GetCount())) {
            last_error = "Spotify resolution cancelled.";
            return false;
        }
        TrackEntry& entry = document.tracks[i];
        if(entry.IsResolved() && entry.state != TRACK_REVIEW)
            continue;
        if(!ResolveTrack(entry))
            return false;
    }
    document.dirty = true;
    return true;
}

bool SpotifyClient::CreatePlaylist(const String& name, bool is_public, const String& description,
                                   SpotifyPlaylistInfo& playlist)
{
    Json json;
    json("name", name)
        ("public", is_public)
        ("collaborative", false)
        ("description", description);

    Value response;
    if(!RequestJson("POST", "/me/playlists", ~json, response) || !IsValueMap(response))
        return false;
    ValueMap item = response;
    playlist.id = VString(item, "id");
    playlist.uri = VString(item, "uri");
    playlist.name = VString(item, "name");
    playlist.snapshot_id = VString(item, "snapshot_id");
    playlist.is_public = VBool(item, "public");
    playlist.collaborative = VBool(item, "collaborative");
    return !playlist.id.IsEmpty();
}

bool SpotifyClient::AddItems(const String& playlist_id, const Vector<String>& uris,
                             String *snapshot_id, int *added_count)
{
    if(added_count)
        *added_count = 0;
    String latest;
    for(int from = 0; from < uris.GetCount(); from += 100) {
        int count = min(100, uris.GetCount() - from);
        Value response;
        if(!RequestJson("POST", "/playlists/" + UrlEncode(playlist_id) + "/items",
                        JsonBodyForUris(uris, from, count), response) || !IsValueMap(response))
            return false;
        latest = VString(ValueMap(response), "snapshot_id");
        if(added_count)
            *added_count += count;
    }
    if(snapshot_id && !latest.IsEmpty())
        *snapshot_id = latest;
    return true;
}

bool SpotifyClient::ReorderItems(const String& playlist_id, const Vector<PlaylistMove>& moves,
                                 String& snapshot_id, int *moves_applied)
{
    if(moves_applied)
        *moves_applied = 0;
    for(const PlaylistMove& move : moves) {
        Json json;
        json("range_start", move.from)
            ("insert_before", move.before)
            ("range_length", move.count);
        if(!snapshot_id.IsEmpty())
            json("snapshot_id", snapshot_id);

        Value response;
        if(!RequestJson("PUT", "/playlists/" + UrlEncode(playlist_id) + "/items", ~json, response) ||
           !IsValueMap(response))
            return false;
        String next_snapshot = VString(ValueMap(response), "snapshot_id");
        if(!next_snapshot.IsEmpty())
            snapshot_id = next_snapshot;
        if(moves_applied)
            (*moves_applied)++;
    }
    return true;
}

bool SpotifyClient::ExecutePublishPreview(const String& playlist_id,
                                          const PlaylistPublishPreview& preview,
                                          const String& expected_snapshot_id,
                                          SpotifyPublishResult& result)
{
    result.Clear();
    last_error.Clear();
    last_status = 0;

    if(playlist_id.IsEmpty()) {
        last_error = "Publish preview has no Spotify target playlist.";
        result.error = last_error;
        return false;
    }
    if(expected_snapshot_id.IsEmpty()) {
        last_error = "Publish preview has no target snapshot; reload the Spotify target before publishing.";
        result.error = last_error;
        return false;
    }
    if(!preview.CanPublish()) {
        last_error = "Publish preview is blocked by unresolved reference rows.";
        result.error = last_error;
        return false;
    }

    Vector<SpotifyTrack> current_tracks;
    String current_snapshot;
    if(!GetPlaylistItems(playlist_id, current_tracks, &current_snapshot)) {
        result.error = last_error;
        return false;
    }
    Vector<String> current_uris = TrackUris(current_tracks);
    result.observed_tracks = pick(current_tracks);
    result.observed_uris = clone(current_uris);
    result.snapshot_id = current_snapshot;
    result.observed = true;

    String validation_error;
    if(!ValidatePlaylistPublishPreview(preview, current_uris, &validation_error)) {
        result.stale = preview.original_target_uris != current_uris;
        last_status = 0;
        last_error = validation_error;
        result.error = last_error;
        return false;
    }
    if(current_snapshot != expected_snapshot_id) {
        result.stale = true;
        last_status = 0;
        last_error = "Spotify target snapshot changed after this preview was created; inspect a fresh preview before publishing.";
        result.error = last_error;
        return false;
    }
    if(preview.IsNoOp()) {
        result.success = true;
        return true;
    }

    auto recover_observed_state = [&](const String& saved_error, int saved_status) {
        Vector<SpotifyTrack> observed;
        String snapshot;
        if(GetPlaylistItems(playlist_id, observed, &snapshot)) {
            result.observed_tracks = pick(observed);
            result.observed_uris = TrackUris(result.observed_tracks);
            result.snapshot_id = snapshot;
            result.observed = true;
        }
        last_error = saved_error;
        last_status = saved_status;
        result.error = saved_error;
    };

    String working_snapshot = current_snapshot;
    if(!preview.add_uris.IsEmpty()) {
        result.observed = false;
        if(!AddItems(playlist_id, preview.add_uris, &working_snapshot, &result.added_count)) {
            String saved_error = last_error;
            int saved_status = last_status;
            result.partial = true; // A failed mutation request can have an unknown remote outcome.
            recover_observed_state(saved_error, saved_status);
            return false;
        }

        Vector<SpotifyTrack> after_add_tracks;
        String after_add_snapshot;
        if(!GetPlaylistItems(playlist_id, after_add_tracks, &after_add_snapshot)) {
            String verify_error = last_error;
            result.partial = true;
            last_status = 0;
            last_error = "Spotify additions were sent, but PlaylistLab could not verify the augmented target: " + verify_error;
            result.error = last_error;
            return false;
        }

        result.observed_tracks = pick(after_add_tracks);
        result.observed_uris = TrackUris(result.observed_tracks);
        result.snapshot_id = after_add_snapshot;
        result.observed = true;
        working_snapshot = after_add_snapshot;
        if(result.observed_uris != preview.reorder_plan.original_uris) {
            result.partial = true;
            last_status = 0;
            last_error = "Spotify target changed around the planned additions; reorder was not attempted. Inspect the refreshed target before retrying.";
            result.error = last_error;
            return false;
        }
    }

    if(!preview.reorder_plan.moves.IsEmpty()) {
        result.observed = false;
        if(!ReorderItems(playlist_id, preview.reorder_plan.moves, working_snapshot, &result.move_count)) {
            String saved_error = last_error;
            int saved_status = last_status;
            result.partial = true; // A failed reorder request can have an unknown remote outcome.
            recover_observed_state(saved_error, saved_status);
            return false;
        }
    }

    Vector<SpotifyTrack> final_tracks;
    String final_snapshot;
    if(!GetPlaylistItems(playlist_id, final_tracks, &final_snapshot)) {
        String verify_error = last_error;
        result.partial = result.added_count > 0 || result.move_count > 0 ||
                         !preview.add_uris.IsEmpty() || !preview.reorder_plan.moves.IsEmpty();
        result.observed = false;
        last_status = 0;
        last_error = "Spotify mutations were sent, but PlaylistLab could not verify the final target: " + verify_error;
        result.error = last_error;
        return false;
    }

    result.observed_tracks = pick(final_tracks);
    result.observed_uris = TrackUris(result.observed_tracks);
    result.snapshot_id = final_snapshot;
    result.observed = true;
    if(result.observed_uris != preview.reorder_plan.desired_uris) {
        result.partial = true;
        last_status = 0;
        last_error = "Spotify target does not match the exact preview after publishing; reload and inspect before retrying.";
        result.error = last_error;
        return false;
    }

    result.success = true;
    result.error.Clear();
    return true;
}

} // namespace Upp
