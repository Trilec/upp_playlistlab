#include "SpotifyClient.h"

namespace Upp {
namespace {

bool IsAppendableSpotifyUri(const String& uri)
{
    return uri.StartsWith("spotify:track:") || uri.StartsWith("spotify:episode:");
}

Vector<String> AppendTrackUris(const Vector<SpotifyTrack>& tracks)
{
    Vector<String> uris;
    uris.Reserve(tracks.GetCount());
    for(const SpotifyTrack& track : tracks)
        uris.Add(track.uri);
    return uris;
}

} // namespace

bool SpotifyClient::ExecuteAppendMissing(const String& playlist_id,
                                         const Vector<String>& working_uris,
                                         const String& expected_snapshot_id,
                                         SpotifyAppendResult& result)
{
    result.Clear();
    last_error.Clear();
    last_status = 0;

    if(playlist_id.IsEmpty()) {
        last_error = "Append operation has no Spotify target playlist.";
        result.error = last_error;
        return false;
    }
    if(expected_snapshot_id.IsEmpty()) {
        last_error = "Append operation has no target snapshot; reload the Spotify target before appending.";
        result.error = last_error;
        return false;
    }
    for(const String& uri : working_uris) {
        if(!IsAppendableSpotifyUri(uri)) {
            last_error = "Append operation contains a non-publishable Spotify item URI.";
            result.error = last_error;
            return false;
        }
    }

    Vector<SpotifyTrack> current_tracks;
    String current_snapshot;
    if(!GetPlaylistItems(playlist_id, current_tracks, &current_snapshot)) {
        result.error = last_error;
        return false;
    }

    Vector<String> current_uris = AppendTrackUris(current_tracks);
    result.observed_tracks = clone(current_tracks);
    result.observed_uris = clone(current_uris);
    result.snapshot_id = current_snapshot;
    result.observed = true;

    if(current_snapshot != expected_snapshot_id) {
        result.stale = true;
        last_status = 0;
        last_error = "Spotify target snapshot changed after Append was prepared; inspect the refreshed target before trying again.";
        result.error = last_error;
        return false;
    }

    result.planned_add_uris = BuildAppendMissingUris(working_uris, current_uris);
    if(result.planned_add_uris.IsEmpty()) {
        result.success = true;
        return true;
    }

    Vector<String> expected_final = clone(current_uris);
    expected_final.Append(result.planned_add_uris);

    String working_snapshot = current_snapshot;
    result.observed = false;
    if(!AddItems(playlist_id, result.planned_add_uris, &working_snapshot, &result.added_count)) {
        String saved_error = last_error;
        int saved_status = last_status;
        result.partial = true;

        Vector<SpotifyTrack> observed;
        String snapshot;
        if(GetPlaylistItems(playlist_id, observed, &snapshot)) {
            result.observed_tracks = pick(observed);
            result.observed_uris = AppendTrackUris(result.observed_tracks);
            result.snapshot_id = snapshot;
            result.observed = true;
        }
        last_error = saved_error;
        last_status = saved_status;
        result.error = saved_error;
        return false;
    }

    Vector<SpotifyTrack> final_tracks;
    String final_snapshot;
    if(!GetPlaylistItems(playlist_id, final_tracks, &final_snapshot)) {
        String verify_error = last_error;
        result.partial = true;
        result.observed = false;
        last_status = 0;
        last_error = "Spotify additions were sent, but PlaylistLab could not verify the final appended target: " + verify_error;
        result.error = last_error;
        return false;
    }

    result.observed_tracks = pick(final_tracks);
    result.observed_uris = AppendTrackUris(result.observed_tracks);
    result.snapshot_id = final_snapshot;
    result.observed = true;
    if(result.observed_uris != expected_final) {
        result.partial = true;
        last_status = 0;
        last_error = "Spotify target changed around Append. Inspect the refreshed playlist before retrying.";
        result.error = last_error;
        return false;
    }

    result.success = true;
    result.error.Clear();
    return true;
}

} // namespace Upp
