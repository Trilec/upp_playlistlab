#include "PlaylistModel.h"

namespace Upp {

bool TrackEntry::IsResolved() const
{
    return !ResolvedUri().IsEmpty();
}

const SpotifyTrack *TrackEntry::GetResolved() const
{
    if(selected_candidate >= 0 && selected_candidate < candidates.GetCount())
        return &candidates[selected_candidate];
    return nullptr;
}

String TrackEntry::ResolvedUri() const
{
    // A best candidate can be preselected for UI review, but it must not become
    // publishable identity until the user confirms it or auto-match accepts it.
    if(state != TRACK_REVIEW)
        if(const SpotifyTrack *track = GetResolved())
            return track->uri;
    return spotify_uri;
}

String TrackEntry::ResolvedTitle() const
{
    if(const SpotifyTrack *track = GetResolved())
        return track->title;
    return requested_title;
}

String TrackEntry::ResolvedArtist() const
{
    if(const SpotifyTrack *track = GetResolved())
        return track->artist;
    return requested_artist;
}

void TrackEntry::SelectCandidate(int i, TrackMatchState new_state)
{
    if(i < 0 || i >= candidates.GetCount()) {
        ClearResolution();
        return;
    }
    selected_candidate = i;
    spotify_uri = candidates[i].uri;
    state = new_state;
}

void TrackEntry::ClearResolution()
{
    selected_candidate = -1;
    spotify_uri.Clear();
    confidence = 0;
    state = TRACK_UNRESOLVED;
    note.Clear();
}

void PlaylistDocument::Clear()
{
    name.Clear();
    source_path.Clear();
    tracks.Clear();
    dirty = false;
}

bool PlaylistDocument::MoveTrack(int from, int before)
{
    int count = tracks.GetCount();
    if(from < 0 || from >= count || before < 0 || before > count)
        return false;
    if(before == from || before == from + 1)
        return false;

    TrackEntry moved = pick(tracks[from]);
    tracks.Remove(from);
    if(before > from)
        before--;
    tracks.Insert(before, pick(moved));
    dirty = true;
    return true;
}

int PlaylistDocument::GetResolvedCount() const
{
    int n = 0;
    for(const auto& track : tracks)
        n += track.IsResolved();
    return n;
}

int PlaylistDocument::GetReviewCount() const
{
    int n = 0;
    for(const auto& track : tracks)
        n += track.state == TRACK_REVIEW;
    return n;
}

int PlaylistDocument::GetMissingCount() const
{
    int n = 0;
    for(const auto& track : tracks)
        n += track.state == TRACK_MISSING;
    return n;
}

Vector<String> PlaylistDocument::GetResolvedUris() const
{
    Vector<String> out;
    for(const auto& track : tracks) {
        String uri = track.ResolvedUri();
        if(!uri.IsEmpty())
            out.Add(uri);
    }
    return out;
}

String TrackMatchStateText(TrackMatchState state)
{
    switch(state) {
    case TRACK_EXACT:      return "Exact";
    case TRACK_AUTO:       return "Matched";
    case TRACK_REVIEW:     return "Review";
    case TRACK_MISSING:    return "Missing";
    default:               return "Unresolved";
    }
}

String NormalizeTrackText(const String& text)
{
    WString out;
    bool gap = false;
    WString src = ToLower(ToUnicode(text, CHARSET_UTF8));
    for(int i = 0; i < src.GetCount(); i++) {
        wchar c = src[i];
        if(IsLetter(c) || IsDigit(c)) {
            if(gap && !out.IsEmpty())
                out.Cat(' ');
            out.Cat(c);
            gap = false;
        }
        else
            gap = true;
    }
    return TrimBoth(FromUnicode(out, CHARSET_UTF8));
}

static int TextScore(const String& requested, const String& candidate, int exact, int contains)
{
    String a = NormalizeTrackText(requested);
    String b = NormalizeTrackText(candidate);
    if(a.IsEmpty())
        return 0;
    if(a == b)
        return exact;
    if(b.Find(a) >= 0 || a.Find(b) >= 0)
        return contains;
    return 0;
}

int ScoreTrackCandidate(const TrackEntry& requested, const SpotifyTrack& candidate)
{
    int score = 0;
    if(!requested.requested_isrc.IsEmpty() &&
       ToUpper(requested.requested_isrc) == ToUpper(candidate.isrc))
        return 100;

    score += TextScore(requested.requested_title, candidate.title, 65, 45);
    score += TextScore(requested.requested_artist, candidate.artist, 30, 18);
    score += TextScore(requested.requested_album, candidate.album, 5, 2);
    return min(score, 100);
}

} // namespace Upp
