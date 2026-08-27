#include "SpotifyClient.h"

namespace Upp {
namespace {

String ReplaceMapString(const ValueMap& map, const char *key)
{
    Value v = map[key];
    return IsNull(v) || v.IsVoid() ? String() : AsString(v);
}

bool IsReplaceableSpotifyUri(const String& uri)
{
    return uri.StartsWith("spotify:track:") || uri.StartsWith("spotify:episode:");
}

String ReplaceJsonBody(const Vector<String>& uris, int from, int count)
{
    JsonArray array;
    for(int i = from; i < from + count; ++i)
        array << uris[i];
    Json json;
    json("uris", array);
    return ~json;
}

Vector<String> ReplaceTrackUris(const Vector<SpotifyTrack>& tracks)
{
    Vector<String> uris;
    uris.Reserve(tracks.GetCount());
    for(const SpotifyTrack& track : tracks)
        uris.Add(track.uri);
    return uris;
}

} // namespace

bool SpotifyClient::ExecuteReplaceItems(const String& playlist_id,
                                        const Vector<String>& desired_uris,
                                        const String& expected_snapshot_id,
                                        SpotifyReplaceResult& result)
{
    result.Clear();
    last_error.Clear();
    last_status = 0;

    if(playlist_id.IsEmpty()) {
        last_error = "Replace operation has no Spotify target playlist.";
        result.error = last_error;
        return false;
    }
    if(expected_snapshot_id.IsEmpty()) {
        last_error = "Replace operation has no target snapshot; reload the Spotify target before replacing it.";
        result.error = last_error;
        return false;
    }
    for(const String& uri : desired_uris) {
        if(!IsReplaceableSpotifyUri(uri)) {
            last_error = "Replace operation contains a non-publishable Spotify item URI.";
            result.error = last_error;
            return false;
        }
    }

    // Replace is deliberately boring about races. If the target is no longer
    // the playlist the user previewed, stop before the destructive request.
    Vector<SpotifyTrack> current_tracks;
    String current_snapshot;
    if(!GetPlaylistItems(playlist_id, current_tracks, &current_snapshot)) {
        result.error = last_error;
        return false;
    }

    Vector<String> current_uris = ReplaceTrackUris(current_tracks);
    result.observed_tracks = pick(current_tracks);
    result.observed_uris = clone(current_uris);
    result.snapshot_id = current_snapshot;
    result.observed = true;

    if(current_snapshot != expected_snapshot_id) {
        result.stale = true;
        last_status = 0;
        last_error = "Spotify target snapshot changed after replacement was prepared; inspect the refreshed target before replacing.";
        result.error = last_error;
        return false;
    }

    if(current_uris == desired_uris) {
        result.success = true;
        result.written_count = desired_uris.GetCount();
        return true;
    }

    auto recover_observed_state = [&](const String& saved_error, int saved_status) {
        // Mutation failures are ambiguous until the remote state is observed.
        // The network can lose the receipt after Spotify has already done the job.
        Vector<SpotifyTrack> observed;
        String snapshot;
        if(GetPlaylistItems(playlist_id, observed, &snapshot)) {
            result.observed_tracks = pick(observed);
            result.observed_uris = ReplaceTrackUris(result.observed_tracks);
            result.snapshot_id = snapshot;
            result.observed = true;
        }
        last_error = saved_error;
        last_status = saved_status;
        result.error = saved_error;
    };

    result.overwritten_count = current_uris.GetCount();
    result.observed = false;

    const int first_count = min(100, desired_uris.GetCount());
    Value response;
    if(!RequestJson("PUT",
                    "/playlists/" + UrlEncode(playlist_id) + "/items",
                    ReplaceJsonBody(desired_uris, 0, first_count),
                    response) || !IsValueMap(response)) {
        String saved_error = last_error;
        int saved_status = last_status;
        result.partial = true; // A failed mutation request can still have changed remote state.
        recover_observed_state(saved_error, saved_status);
        return false;
    }

    String working_snapshot = ReplaceMapString(ValueMap(response), "snapshot_id");
    result.written_count = first_count;

    if(desired_uris.GetCount() > first_count) {
        Vector<String> tail;
        tail.Reserve(desired_uris.GetCount() - first_count);
        for(int i = first_count; i < desired_uris.GetCount(); ++i)
            tail.Add(desired_uris[i]);

        int added = 0;
        if(!AddItems(playlist_id, tail, &working_snapshot, &added)) {
            String saved_error = last_error;
            int saved_status = last_status;
            result.appended_count = added;
            result.written_count += added;
            result.partial = true;
            recover_observed_state(saved_error, saved_status);
            return false;
        }
        result.appended_count = added;
        result.written_count += added;
    }

    Vector<SpotifyTrack> final_tracks;
    String final_snapshot;
    if(!GetPlaylistItems(playlist_id, final_tracks, &final_snapshot)) {
        String verify_error = last_error;
        result.partial = true;
        result.observed = false;
        last_status = 0;
        last_error = "Spotify replacement was sent, but PlaylistLab could not verify the final target: " + verify_error;
        result.error = last_error;
        return false;
    }

    result.observed_tracks = pick(final_tracks);
    result.observed_uris = ReplaceTrackUris(result.observed_tracks);
    result.snapshot_id = final_snapshot;
    result.observed = true;
    if(result.observed_uris != desired_uris) {
        result.partial = true;
        last_status = 0;
        last_error = "Spotify target does not exactly match the requested replacement; reload and inspect before retrying.";
        result.error = last_error;
        return false;
    }

    result.success = true;
    result.error.Clear();
    return true;
}

} // namespace Upp
