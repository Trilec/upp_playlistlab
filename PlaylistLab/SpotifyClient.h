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

class SpotifyClient {
public:
    explicit SpotifyClient(SpotifyAuth& auth);

    String GetLastError() const                         { return last_error; }
    int    GetLastStatus() const                        { return last_status; }

    bool GetCurrentUser(String& user_id, String& display_name);
    bool GetPlaylists(Vector<SpotifyPlaylistInfo>& playlists);
    bool GetEditablePlaylists(Vector<SpotifyPlaylistInfo>& playlists);
    bool GetPlaylistItems(const String& playlist_id, Vector<SpotifyTrack>& tracks, String *snapshot_id = nullptr);
    bool SearchTracks(const TrackEntry& request, Vector<SpotifyTrack>& tracks, int limit = 10);
    bool ResolveTrack(TrackEntry& entry);
    bool ResolveDocument(PlaylistDocument& document, Gate<int, int> progress = Gate<int, int>());

    bool CreatePlaylist(const String& name, bool is_public, const String& description,
                        SpotifyPlaylistInfo& playlist);
    bool AddItems(const String& playlist_id, const Vector<String>& uris,
                  String *snapshot_id = nullptr, int *added_count = nullptr);
    bool ReorderItems(const String& playlist_id, const Vector<PlaylistMove>& moves,
                      String& snapshot_id, int *moves_applied = nullptr);
    bool ExecutePublishPreview(const String& playlist_id,
                               const PlaylistPublishPreview& preview,
                               const String& expected_snapshot_id,
                               SpotifyPublishResult& result);

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
