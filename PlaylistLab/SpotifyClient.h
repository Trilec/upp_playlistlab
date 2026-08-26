#ifndef _PlaylistLab_SpotifyClient_h_
#define _PlaylistLab_SpotifyClient_h_

#include "PlaylistModel.h"
#include "PlaylistPlanner.h"
#include "SpotifyAuth.h"

namespace Upp {

struct SpotifyPublishResult {
    bool success = false;
    bool stale = false;
    bool partial = false;
    bool observed = false;
    int  added_count = 0;
    int  move_count = 0;
    String snapshot_id;
    String error;
    Vector<SpotifyTrack> observed_tracks;
    Vector<String>       observed_uris;

    void Clear()
    {
        success = false;
        stale = false;
        partial = false;
        observed = false;
        added_count = 0;
        move_count = 0;
        snapshot_id.Clear();
        error.Clear();
        observed_tracks.Clear();
        observed_uris.Clear();
    }
};

struct SpotifyAppendResult {
    bool success = false;
    bool stale = false;
    bool partial = false;
    bool observed = false;
    int  added_count = 0;
    String snapshot_id;
    String error;
    Vector<String> planned_add_uris;
    Vector<SpotifyTrack> observed_tracks;
    Vector<String>       observed_uris;

    void Clear()
    {
        success = false;
        stale = false;
        partial = false;
        observed = false;
        added_count = 0;
        snapshot_id.Clear();
        error.Clear();
        planned_add_uris.Clear();
        observed_tracks.Clear();
        observed_uris.Clear();
    }
};

struct SpotifyReplaceResult {
    bool success = false;
    bool stale = false;
    bool partial = false;
    bool observed = false;
    int  overwritten_count = 0;
    int  written_count = 0;
    int  appended_count = 0;
    String snapshot_id;
    String error;
    Vector<SpotifyTrack> observed_tracks;
    Vector<String>       observed_uris;

    void Clear()
    {
        success = false;
        stale = false;
        partial = false;
        observed = false;
        overwritten_count = 0;
        written_count = 0;
        appended_count = 0;
        snapshot_id.Clear();
        error.Clear();
        observed_tracks.Clear();
        observed_uris.Clear();
    }
};

class SpotifyClient {
public:
    explicit SpotifyClient(SpotifyAuth& auth);

    String GetLastError() const                         { return last_error; }
    int    GetLastStatus() const                        { return last_status; }

    bool GetCurrentUser(String& user_id, String& display_name);
    bool GetPlaylists(Vector<SpotifyPlaylistInfo>& playlists);
    bool GetEditablePlaylists(Vector<SpotifyPlaylistInfo>& playlists);
    bool GetPlaylistItems(const String& playlist_id, Vector<SpotifyTrack>& tracks, String *snapshot_id = nullptr);
    bool SearchTracks(const String& query, Vector<SpotifyTrack>& tracks, int limit = 10);
    bool SearchTracks(const TrackEntry& request, Vector<SpotifyTrack>& tracks, int limit = 10);
    bool ResolveTrack(TrackEntry& entry);
    bool ResolveDocument(PlaylistDocument& document, Gate<int, int> progress = Gate<int, int>());

    bool CreatePlaylist(const String& name, bool is_public, const String& description,
                        SpotifyPlaylistInfo& playlist);
    bool UpdatePlaylistDetails(const String& playlist_id, const String& name);
    bool AddItems(const String& playlist_id, const Vector<String>& uris,
                  String *snapshot_id = nullptr, int *added_count = nullptr);
    bool ReorderItems(const String& playlist_id, const Vector<PlaylistMove>& moves,
                      String& snapshot_id, int *moves_applied = nullptr);
    bool ExecutePublishPreview(const String& playlist_id,
                               const PlaylistPublishPreview& preview,
                               const String& expected_snapshot_id,
                               SpotifyPublishResult& result);

    // Explicit append-only operation for the simplified authoring workflow.
    // Missing occurrences are computed from a fresh stable target read, appended
    // in Working order, and the exact augmented target is read back before success.
    bool ExecuteAppendMissing(const String& playlist_id,
                              const Vector<String>& working_uris,
                              const String& expected_snapshot_id,
                              SpotifyAppendResult& result);

    // Explicit destructive operation. This never runs as part of the guarded
    // add/reorder publisher. The target must still match expected_snapshot_id;
    // the exact desired sequence is read back before success is reported.
    bool ExecuteReplaceItems(const String& playlist_id,
                             const Vector<String>& desired_uris,
                             const String& expected_snapshot_id,
                             SpotifyReplaceResult& result);

private:
    SpotifyAuth& auth;
    String       last_error;
    int          last_status = 0;

    bool Request(const String& method, const String& url_or_path, const String& body,
                 String& response, bool allow_refresh = true);
    bool RequestJson(const String& method, const String& url_or_path, const String& body,
                     Value& response);
    bool JsonTrack(const Value& value, SpotifyTrack& track) const;
    String ApiUrl(const String& path) const;
    void SetApiError(int status, const String& body);
};

} // namespace Upp

#endif
