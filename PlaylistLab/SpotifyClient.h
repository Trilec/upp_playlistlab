#ifndef _PlaylistLab_SpotifyClient_h_
#define _PlaylistLab_SpotifyClient_h_

#include "PlaylistModel.h"
#include "PlaylistPlanner.h"
#include "SpotifyAuth.h"

namespace Upp {

class SpotifyClient {
public:
    explicit SpotifyClient(SpotifyAuth& auth);

    String GetLastError() const                         { return last_error; }
    int    GetLastStatus() const                        { return last_status; }

    bool GetCurrentUser(String& user_id, String& display_name);
    bool GetEditablePlaylists(Vector<SpotifyPlaylistInfo>& playlists);
    bool GetPlaylistItems(const String& playlist_id, Vector<SpotifyTrack>& tracks, String *snapshot_id = nullptr);
    bool SearchTracks(const TrackEntry& request, Vector<SpotifyTrack>& tracks, int limit = 10);
    bool ResolveTrack(TrackEntry& entry);
    bool ResolveDocument(PlaylistDocument& document, Gate<int, int> progress = Gate<int, int>());

    bool CreatePlaylist(const String& name, bool is_public, const String& description,
                        SpotifyPlaylistInfo& playlist);
    bool AddItems(const String& playlist_id, const Vector<String>& uris, String *snapshot_id = nullptr);
    bool ReorderItems(const String& playlist_id, const Vector<PlaylistMove>& moves,
                      String& snapshot_id);

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
