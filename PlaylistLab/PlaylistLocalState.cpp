#include "PlaylistLocalState.h"

namespace Upp {
namespace {

String LSString(const ValueMap& map, const char *key)
{
    Value v = map[key];
    return IsNull(v) || v.IsVoid() ? String() : AsString(v);
}

int LSInt(const ValueMap& map, const char *key, int fallback = 0)
{
    Value v = map[key];
    return IsNull(v) || v.IsVoid() ? fallback : (int)v;
}

bool LSBool(const ValueMap& map, const char *key, bool fallback = false)
{
    Value v = map[key];
    return IsNull(v) || v.IsVoid() ? fallback : (bool)v;
}

ValueArray LSArray(const ValueMap& map, const char *key)
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
    track.id = LSString(map, "id");
    track.uri = LSString(map, "uri");
    track.title = LSString(map, "title");
    track.artist = LSString(map, "artist");
    track.album = LSString(map, "album");
    track.isrc = LSString(map, "isrc");
    track.image_url = LSString(map, "image_url");
    track.spotify_url = LSString(map, "spotify_url");
    track.type = LSString(map, "type");
    if(track.type.IsEmpty())
        track.type = "track";
    track.placeholder = LSBool(map, "placeholder");
    track.duration_ms = LSInt(map, "duration_ms");
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
        ("user_note", entry.user_note)
        ("selected_candidate", entry.selected_candidate)
        ("candidates", candidates);
    return json;
}

bool ReadEntry(const Value& value, TrackEntry& entry)
{
    if(!IsValueMap(value))
        return false;
    ValueMap map = value;
    entry.requested_title = LSString(map, "requested_title");
    entry.requested_artist = LSString(map, "requested_artist");
    entry.requested_album = LSString(map, "requested_album");
    entry.requested_isrc = LSString(map, "requested_isrc");
    entry.spotify_uri = LSString(map, "spotify_uri");
    int state = LSInt(map, "state", (int)TRACK_UNRESOLVED);
    entry.state = state >= (int)TRACK_UNRESOLVED && state <= (int)TRACK_MISSING
                ? (TrackMatchState)state : TRACK_UNRESOLVED;
    entry.confidence = minmax(LSInt(map, "confidence"), 0, 100);
    entry.note = LSString(map, "note");
    entry.user_note = LSString(map, "user_note");
    entry.selected_candidate = LSInt(map, "selected_candidate", -1);

    ValueArray candidates = LSArray(map, "candidates");
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
    ValueArray items = LSArray(root, "profiles");
    Index<String> seen;
    for(int i = 0; i < items.GetCount() && profiles.GetCount() < 32; i++) {
        if(!IsValueMap(items[i]))
            continue;
        ValueMap map = items[i];
        String client_id = TrimBoth(LSString(map, "client_id"));
        if(client_id.IsEmpty() || seen.Find(client_id) >= 0)
            continue;
        String name = TrimBoth(LSString(map, "name"));
        if(name.IsEmpty())
            name = Format("Spotify Client %d", profiles.GetCount() + 1);
        SpotifyClientProfile& profile = profiles.Add();
        profile.name = name;
        profile.client_id = client_id;
        seen.Add(client_id);
    }

    selected_profile = LSInt(root, "selected_profile", profiles.IsEmpty() ? -1 : 0);
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
    document.name = LSString(root, "name");
    document.source_path = LSString(root, "source_path");
    document.dirty = LSBool(root, "dirty");
    source_label = LSString(root, "source_label");

    ValueArray tracks = LSArray(root, "tracks");
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
    root("version", 2)
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
