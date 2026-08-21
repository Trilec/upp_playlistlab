#ifndef _PlaylistLab_PlaylistModel_h_
#define _PlaylistLab_PlaylistModel_h_

#include <Core/Core.h>

namespace Upp {

enum TrackMatchState {
    TRACK_UNRESOLVED,
    TRACK_EXACT,
    TRACK_AUTO,
    TRACK_REVIEW,
    TRACK_MISSING,
};

struct SpotifyTrack : Moveable<SpotifyTrack> {
    String id;
    String uri;
    String title;
    String artist;
    String album;
    String isrc;
    String image_url;
    String spotify_url;
    String type = "track";
    bool   placeholder = false;
    int    duration_ms = 0;

    bool IsValid() const { return !uri.IsEmpty(); }
};

struct TrackEntry : Moveable<TrackEntry> {
    String requested_title;
    String requested_artist;
    String requested_album;
    String requested_isrc;
    String spotify_uri;

    Vector<SpotifyTrack> candidates;
    int             selected_candidate = -1;
    TrackMatchState state = TRACK_UNRESOLVED;
    int             confidence = 0;
    String          note;

    bool IsResolved() const;
    const SpotifyTrack *GetResolved() const;
    String ResolvedUri() const;
    String ResolvedTitle() const;
    String ResolvedArtist() const;
    void SelectCandidate(int i, TrackMatchState new_state = TRACK_EXACT);
    void ClearResolution();
};

struct PlaylistDocument {
    String             name;
    String             source_path;
    Vector<TrackEntry> tracks;
    bool               dirty = false;

    void Clear();
    bool MoveTrack(int from, int before);
    int  GetResolvedCount() const;
    int  GetReviewCount() const;
    int  GetMissingCount() const;
    Vector<String> GetResolvedUris() const;
};

struct SpotifyPlaylistInfo : Moveable<SpotifyPlaylistInfo> {
    String id;
    String uri;
    String name;
    String owner_id;
    String owner_name;
    String snapshot_id;
    String image_url;
    String spotify_url;
    int    item_count = 0;
    bool   collaborative = false;
    bool   is_public = false;
    bool   editable = false;
    bool   items_accessible = false;
};

TrackEntry       CloneTrackEntry(const TrackEntry& source);
PlaylistDocument ClonePlaylistDocument(const PlaylistDocument& source);
String            TrackMatchStateText(TrackMatchState state);
String            NormalizeTrackText(const String& text);
int               ScoreTrackCandidate(const TrackEntry& requested, const SpotifyTrack& candidate);

} // namespace Upp

#endif
