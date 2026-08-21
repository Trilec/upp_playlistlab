#include "PlaylistLocalState.h"

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

ValueArray VArray(const ValueMap& map, const char *key)
{
    Value v = map[key];
    return IsValueArray(v) ? ValueArray(v) : ValueArray();
}

void ClearError(String *error)
{
    if(error)
        error->Clear();
}

bool Fail(String *error, const String& message)
{
    if(error)
        *error = message;
    return false;
}

Json TrackJson(const SpotifyTrack& track)
{
    Json json;
    json("id", track.id)
        ("uri", track.uri)
        ("title", track.title)
        ("artist", track.artist)
        ("album", track.album)
        ("isrc", track.isrc)
        ("image_url", track.image_url)
        ("spotify_url", track.spotify_url)
        ("type", track.type)
        ("placeholder", track.placeholder)
        ("duration_ms", track.duration_ms);
    return json;
}

bool ReadTrack(const Value& value, SpotifyTrack& track)
{
    if(!IsValueMap(value))
        return false;
    ValueMap map = value;
    track.id = VString(map, "id");
    track.uri = VString(map, "uri");
    track.title = VString(map, "title");
    track.artist = VString(map, "artist");
    track.album = VString(map, "album");
    track.isrc = VString(map, "isrc");
    track.image_url = VString(map, "image_url");
    track.spotify_url = VString(map, "spotify_url");
    track.type = VString(map, "type");
    if(track.type.IsEmpty())
        track.type = "track";
    track.placeholder = VBool(map, "placeholder");
    track.duration_ms = VInt(map, "duration_ms");
    return true;
}

Json EntryJson(const TrackEntry& entry)
{
    JsonArray candidates;
    for(const SpotifyTrack& track : entry.candidates)
        candidates << TrackJson(track);

    Json json;
    json("requested_title", entry.requested_title)
        ("requested_artist", entry.requested_artist)
        ("requested_album", entry.requested_album)
        ("requested_isrc", entry.requested_isrc)
        ("spotify_uri", entry.spotify_uri)
        ("state", (int)entry.state)
        ("confidence", entry.confidence)
        ("note", entry.note)
        ("selected_candidate", entry.selected_candidate)
        ("candidates", candidates);
    return json;
}

bool ReadEntry(const Value& value, TrackEntry& entry)
{
    if(!IsValueMap(value))
        return false;
    ValueMap map = value;
    entry.requested_title = VString(map, "requested_title");
    entry.requested_artist = VString(map, "requested_artist");
    entry.requested_album = VString(map, "requested_album");
    entry.requested_isrc = VString(map, "requested_isrc");
    entry.spotify_uri = VString(map, "spotify_uri");
    int state = VInt(map, "state", (int)TRACK_UNRESOLVED);
    entry.state = state >= (int)TRACK_UNRESOLVED && state <= (int)TRACK_MISSING
                ? (TrackMatchState)state : TRACK_UNRESOLVED;
    entry.confidence = minmax(VInt(map, "confidence"), 0, 100);
    entry.note = VString(map, "note");
    entry.selected_candidate = VInt(map, "selected_candidate", -1);

    ValueArray candidates = VArray(map, "candidates");
    for(int i = 0; i < candidates.GetCount(); i++) {
        SpotifyTrack track;
        if(ReadTrack(candidates[i], track))
            entry.candidates.Add(pick(track));
    }
    if(entry.selected_candidate < 0 || entry.selected_candidate >= entry.candidates.GetCount())
        entry.selected_candidate = -1;
    return true;
}

} // namespace

String PlaylistLocalState::ProfilesPath()
{
    return ConfigFile("playlistlab.clients.json");
}

String PlaylistLocalState::WorkingPath()
{
    return ConfigFile("playlistlab.working.json");
}

bool PlaylistLocalState::LoadProfiles(Vector<SpotifyClientProfile>& profiles,
                                      int& selected_profile,
                                      String *error)
{
    ClearError(error);
    profiles.Clear();
    selected_profile = -1;

    String text = LoadFile(ProfilesPath());
    if(text.IsEmpty())
        return true;

    Value parsed;
    try {
        parsed = ParseJSON(text);
    }
    catch(...) {
        return Fail(error, "Stored Spotify Client ID profiles are not valid JSON.");
    }
    if(!IsValueMap(parsed))
        return Fail(error, "Stored Spotify Client ID profiles have an invalid root object.");

    ValueMap root = parsed;
    ValueArray items = VArray(root, "profiles");
    Index<String> seen;
    for(int i = 0; i < items.GetCount() && profiles.GetCount() < 32; i++) {
        if(!IsValueMap(items[i]))
            continue;
        ValueMap map = items[i];
        String client_id = TrimBoth(VString(map, "client_id"));
        if(client_id.IsEmpty() || seen.Find(client_id) >= 0)
            continue;
        String name = TrimBoth(VString(map, "name"));
        if(name.IsEmpty())
            name = Format("Spotify Client %d", profiles.GetCount() + 1);
        SpotifyClientProfile& profile = profiles.Add();
        profile.name = name;
        profile.client_id = client_id;
        seen.Add(client_id);
    }

    selected_profile = VInt(root, "selected_profile", profiles.IsEmpty() ? -1 : 0);
    if(selected_profile < 0 || selected_profile >= profiles.GetCount())
        selected_profile = profiles.IsEmpty() ? -1 : 0;
    return true;
}

bool PlaylistLocalState::SaveProfiles(const Vector<SpotifyClientProfile>& profiles,
                                      int selected_profile,
                                      String *error)
{
    ClearError(error);
    JsonArray items;
    for(const SpotifyClientProfile& profile : profiles) {
        if(TrimBoth(profile.client_id).IsEmpty())
            continue;
        Json item;
        item("name", TrimBoth(profile.name))
            ("client_id", TrimBoth(profile.client_id));
        items << item;
    }

    int selected = selected_profile;
    if(selected < 0 || selected >= profiles.GetCount())
        selected = profiles.IsEmpty() ? -1 : 0;

    Json root;
    root("version", 1)
        ("selected_profile", selected)
        ("profiles", items);
    if(!SaveFile(ProfilesPath(), ~root))
        return Fail(error, "PlaylistLab could not save Spotify Client ID profiles.");
    return true;
}

bool PlaylistLocalState::LoadWorking(PlaylistDocument& document,
                                     String& source_label,
                                     String *error)
{
    ClearError(error);
    document.Clear();
    source_label.Clear();

    String text = LoadFile(WorkingPath());
    if(text.IsEmpty())
        return true;

    Value parsed;
    try {
        parsed = ParseJSON(text);
    }
    catch(...) {
        return Fail(error, "Stored Working Playlist state is not valid JSON.");
    }
    if(!IsValueMap(parsed))
        return Fail(error, "Stored Working Playlist state has an invalid root object.");

    ValueMap root = parsed;
    document.name = VString(root, "name");
    document.source_path = VString(root, "source_path");
    document.dirty = VBool(root, "dirty");
    source_label = VString(root, "source_label");

    ValueArray tracks = VArray(root, "tracks");
    document.tracks.Reserve(tracks.GetCount());
    for(int i = 0; i < tracks.GetCount(); i++) {
        TrackEntry entry;
        if(ReadEntry(tracks[i], entry))
            document.tracks.Add(pick(entry));
    }
    return true;
}

bool PlaylistLocalState::SaveWorking(const PlaylistDocument& document,
                                     const String& source_label,
                                     String *error)
{
    ClearError(error);
    JsonArray tracks;
    for(const TrackEntry& entry : document.tracks)
        tracks << EntryJson(entry);

    Json root;
    root("version", 1)
        ("name", document.name)
        ("source_path", document.source_path)
        ("dirty", document.dirty)
        ("source_label", source_label)
        ("tracks", tracks);

    if(!SaveFile(WorkingPath(), ~root))
        return Fail(error, "PlaylistLab could not save the Working Playlist state.");
    return true;
}

} // namespace Upp
