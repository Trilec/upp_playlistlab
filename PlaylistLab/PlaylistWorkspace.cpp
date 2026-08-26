#ifdef flagMAIN

#include <CtrlLib/CtrlLib.h>
#include <Ui/Ui.h>
#include "PlaylistIO.h"
#include "PlaylistPlanner.h"
#include "PlaylistLocalState.h"
#include "SpotifyClient.h"
#include "SpotifyImageCache.h"
#include "PlaylistTransferList.h"

using namespace Upp;

namespace {

const UiThemePreset APP_THEME = UiThemePreset::Minimal;
const UiThemeMode   APP_MODE  = UiThemeMode::Dark;

enum TransferAction {
    TRANSFER_SELECTED = 0,
    TRANSFER_ALL = 1,
};

enum DestinationAction {
    DEST_CREATE = 0,
    DEST_APPEND = 1,
    DEST_REPLACE = 2,
};

Color AppBackground() { return Color(18, 18, 18); }

String ValueString(const ValueMap& map, const char *key)
{
    Value v = map[key];
    return IsNull(v) || v.IsVoid() ? String() : AsString(v);
}

Color MatchStateColor(TrackMatchState state)
{
    switch(state) {
    case TRACK_EXACT:      return Color(34, 197, 94);
    case TRACK_AUTO:       return Color(59, 130, 246);
    case TRACK_REVIEW:     return Color(245, 158, 11);
    case TRACK_MISSING:    return Color(239, 68, 68);
    default:               return Color(148, 163, 184);
    }
}

String TrackDisplayTitle(const TrackEntry& entry)
{
    String title = entry.ResolvedTitle();
    if(title.IsEmpty())
        title = entry.ResolvedUri();
    return title.IsEmpty() ? String("Untitled track") : title;
}

String TrackDisplayDescription(const TrackEntry& entry)
{
    String artist = entry.ResolvedArtist();
    String album;
    if(const SpotifyTrack *resolved = entry.GetResolved())
        album = resolved->album;
    else if(entry.selected_candidate >= 0 && entry.selected_candidate < entry.candidates.GetCount())
        album = entry.candidates[entry.selected_candidate].album;
    else
        album = entry.requested_album;

    String out = artist;
    if(!album.IsEmpty()) {
        if(!out.IsEmpty())
            out << "  •  ";
        out << album;
    }
    return out;
}

String SpotifyTrackDescription(const SpotifyTrack& track)
{
    String out = track.artist;
    if(!track.album.IsEmpty()) {
        if(!out.IsEmpty())
            out << "  •  ";
        out << track.album;
    }
    return out;
}

String DurationText(int duration_ms)
{
    if(duration_ms <= 0)
        return String();
    int seconds = duration_ms / 1000;
    return Format("%d:%02d", seconds / 60, seconds % 60);
}

bool IsSpotifyPublishableUri(const String& uri)
{
    return uri.StartsWith("spotify:track:") || uri.StartsWith("spotify:episode:");
}

void CopySpotifyTrack(SpotifyTrack& dst, const SpotifyTrack& src)
{
    dst.id = src.id;
    dst.uri = src.uri;
    dst.title = src.title;
    dst.artist = src.artist;
    dst.album = src.album;
    dst.isrc = src.isrc;
    dst.image_url = src.image_url;
    dst.spotify_url = src.spotify_url;
    dst.type = src.type;
    dst.placeholder = src.placeholder;
    dst.duration_ms = src.duration_ms;
}

TrackEntry EntryFromSpotify(const SpotifyTrack& track)
{
    TrackEntry entry;
    entry.requested_title = track.title;
    entry.requested_artist = track.artist;
    entry.requested_album = track.album;
    entry.requested_isrc = track.isrc;
    if(!track.placeholder && IsSpotifyPublishableUri(track.uri)) {
        SpotifyTrack& candidate = entry.candidates.Add();
        CopySpotifyTrack(candidate, track);
        entry.selected_candidate = 0;
        entry.spotify_uri = track.uri;
        entry.state = TRACK_EXACT;
        entry.confidence = 100;
        entry.note = "Loaded directly from Spotify";
    }
    else {
        entry.state = TRACK_MISSING;
        entry.note = "Unavailable Spotify playlist position";
    }
    return entry;
}

const SpotifyTrack *ArtworkTrack(const TrackEntry& entry)
{
    if(const SpotifyTrack *resolved = entry.GetResolved())
        return resolved;
    if(entry.selected_candidate >= 0 && entry.selected_candidate < entry.candidates.GetCount())
        return &entry.candidates[entry.selected_candidate];
    return nullptr;
}

String TrackArtworkKey(const SpotifyTrack& track)
{
    return !track.id.IsEmpty() ? track.id : track.uri;
}

Vector<int> SelectedIndices(const UiList& list)
{
    Vector<int> out = list.GetSelection();
    Sort(out);
    return out;
}

int CountOccurrenceDifference(const Vector<String>& from, const Vector<String>& to)
{
    VectorMap<String, int> available;
    for(const String& uri : to) {
        int q = available.Find(uri);
        if(q < 0)
            available.Add(uri, 1);
        else
            available[q]++;
    }
    int difference = 0;
    for(const String& uri : from) {
        int q = available.Find(uri);
        if(q < 0 || available[q] <= 0)
            difference++;
        else
            available[q]--;
    }
    return difference;
}

String ProfileDisplay(const SpotifyClientProfile& profile)
{
    String id = TrimBoth(profile.client_id);
    String suffix = id.GetCount() > 6 ? id.Right(6) : id;
    String name = TrimBoth(profile.name);
    if(name.IsEmpty())
        name = "Spotify Client";
    return suffix.IsEmpty() ? name : name + "  ·  …" + suffix;
}

String DestinationText(int action)
{
    switch(action) {
    case DEST_APPEND:  return "Append Missing";
    case DEST_REPLACE: return "Replace";
    default:           return "Create New";
    }
}

String TransferText(int action)
{
    return action == TRANSFER_ALL ? "Add All" : "Add Selected";
}

class UiChoiceDialog : public TopWindow {
public:
    typedef UiChoiceDialog CLASSNAME;

    UiChoiceDialog(const String& title, const Vector<UiModelItem>& rows)
    {
        Title(title);
        Sizeable().Zoomable();
        SetRect(0, 0, DPI(700), DPI(500));
        Add(list_);
        Add(ok_);
        Add(cancel_);

        list_.SetModel(model_)
             .EnableRenameOnDblClick(false)
             .EnableDragReorder(false)
             .ShowDragHandle(false);
        model_.AddRange(rows);

        UiList::Style list_style = UiTheme::ResolveList(APP_THEME, APP_MODE);
        list_style.row_height = DPI(50);
        list_style.show_checks = false;
        list_style.show_icons = false;
        list_style.show_metadata_marker = false;
        list_style.right_text_as_badge = false;
        list_.SetCustomStyle(list_style);

        ok_.SetText("Select");
        cancel_.SetText("Cancel");
        ok_.SetCustomStyle(UiTheme::ResolveButton(APP_THEME, APP_MODE, UiButtonRole::Accent));
        cancel_.SetCustomStyle(UiTheme::ResolveButton(APP_THEME, APP_MODE, UiButtonRole::Subtle));
        ok_.WhenAction = [=] {
            if(list_.GetCursor() >= 0)
                AcceptBreak(IDOK);
        };
        cancel_.WhenAction = [=] { RejectBreak(IDCANCEL); };
    }

    virtual void Paint(Draw& w) override { w.DrawRect(GetSize(), AppBackground()); }

    virtual void Layout() override
    {
        Rect rc = GetSize();
        int margin = DPI(14), gap = DPI(8), button_h = DPI(34), button_w = DPI(96);
        int y = max(margin, rc.GetHeight() - margin - button_h);
        list_.SetRect(margin, margin, max(0, rc.GetWidth() - margin * 2), max(0, y - margin - gap));
        cancel_.SetRect(max(margin, rc.GetWidth() - margin - button_w), y, button_w, button_h);
        ok_.SetRect(max(margin, rc.GetWidth() - margin - button_w * 2 - gap), y, button_w, button_h);
    }

    int Choose(int initial = 0)
    {
        if(model_.GetCount() > 0)
            list_.SetCursor(minmax(initial, 0, model_.GetCount() - 1));
        return ExecuteOK() ? list_.GetCursor() : -1;
    }

private:
    UiListModel model_;
    UiList list_;
    UiButton ok_, cancel_;
};

class ProfileDialog : public TopWindow {
public:
    typedef ProfileDialog CLASSNAME;
    enum { PROFILE_DELETE = 2001 };

    ProfileDialog(const String& title, const String& name, const String& client_id, bool allow_delete)
    {
        Title(title);
        SetRect(0, 0, DPI(520), DPI(245));
        Add(name_label_); Add(name_);
        Add(id_label_); Add(client_id_);
        Add(save_); Add(cancel_); Add(remove_);

        name_label_.SetText("Friendly name");
        id_label_.SetText("Spotify Client ID");
        name_.SetTextUtf8(name);
        client_id_.SetTextUtf8(client_id);
        save_.SetText("Save");
        cancel_.SetText("Cancel");
        remove_.SetText("Delete Profile");
        remove_.Show(allow_delete);

        UiLabel::Style caption = UiTheme::ResolveLabel(APP_THEME, APP_MODE, UiLabelRole::Caption);
        name_label_.SetCustomStyle(caption);
        id_label_.SetCustomStyle(caption);
        save_.SetCustomStyle(UiTheme::ResolveButton(APP_THEME, APP_MODE, UiButtonRole::Accent));
        cancel_.SetCustomStyle(UiTheme::ResolveButton(APP_THEME, APP_MODE, UiButtonRole::Subtle));
        remove_.SetCustomStyle(UiTheme::ResolveButton(APP_THEME, APP_MODE, UiButtonRole::Subtle));

        save_.WhenAction = [=] { AcceptBreak(IDOK); };
        cancel_.WhenAction = [=] { RejectBreak(IDCANCEL); };
        remove_.WhenAction = [=] { AcceptBreak(PROFILE_DELETE); };
        client_id_.WhenAction = [=] { AcceptBreak(IDOK); };
    }

    virtual void Paint(Draw& w) override { w.DrawRect(GetSize(), AppBackground()); }

    virtual void Layout() override
    {
        Rect rc = GetSize();
        int margin = DPI(16), w = max(0, rc.GetWidth() - margin * 2), y = margin;
        name_label_.SetRect(margin, y, w, DPI(20)); y += DPI(22);
        name_.SetRect(margin, y, w, DPI(34)); y += DPI(46);
        id_label_.SetRect(margin, y, w, DPI(20)); y += DPI(22);
        client_id_.SetRect(margin, y, w, DPI(34));

        int by = rc.GetHeight() - margin - DPI(34);
        if(remove_.IsShown())
            remove_.SetRect(margin, by, DPI(122), DPI(34));
        cancel_.SetRect(rc.GetWidth() - margin - DPI(96), by, DPI(96), DPI(34));
        save_.SetRect(rc.GetWidth() - margin - DPI(200), by, DPI(96), DPI(34));
    }

    String GetName() const { return TrimBoth(name_.GetTextUtf8()); }
    String GetClientId() const { return TrimBoth(client_id_.GetTextUtf8()); }

private:
    UiLabel name_label_, id_label_;
    UiLineEdit name_, client_id_;
    UiButton save_, cancel_, remove_;
};

class PreviewDialog : public TopWindow {
public:
    typedef PreviewDialog CLASSNAME;
    enum { PULSE_ID = 7301 };

    PreviewDialog(const String& title,
                  const String& context,
                  const String& summary,
                  const String& detail,
                  const Vector<UiModelItem>& rows,
                  const String& action_text,
                  bool can_action)
    {
        Title(title);
        Sizeable().Zoomable();
        SetRect(0, 0, DPI(860), DPI(620));
        Add(context_); Add(summary_); Add(detail_); Add(list_); Add(action_); Add(cancel_);

        context_.SetText(context);
        summary_.SetText(summary);
        detail_.SetText(detail);
        context_.SetCustomStyle(UiTheme::ResolveLabel(APP_THEME, APP_MODE, UiLabelRole::Title));
        summary_.SetCustomStyle(UiTheme::ResolveLabel(APP_THEME, APP_MODE, UiLabelRole::Body));
        detail_.SetCustomStyle(UiTheme::ResolveLabel(APP_THEME, APP_MODE, UiLabelRole::Caption));

        action_.SetText(action_text);
        action_.Enable(can_action);
        cancel_.SetText("Cancel");
        cancel_.SetCustomStyle(UiTheme::ResolveButton(APP_THEME, APP_MODE, UiButtonRole::Subtle));
        ApplyConfirmStyle();
        action_.WhenAction = [=] { AcceptBreak(IDYES); };
        cancel_.WhenAction = [=] { RejectBreak(IDCANCEL); };

        list_.SetModel(model_)
             .EnableRenameOnDblClick(false)
             .EnableDragReorder(false)
             .ShowDragHandle(false);
        model_.AddRange(rows);

        UiList::Style style = UiTheme::ResolveList(APP_THEME, APP_MODE);
        style.row_height = DPI(46);
        style.show_checks = false;
        style.show_icons = false;
        style.show_metadata_marker = false;
        style.right_text_as_badge = true;
        style.badge_radius = DPI(8);
        list_.SetCustomStyle(style);
        SetTimeCallback(650, THISBACK(Pulse), PULSE_ID);
    }

    ~PreviewDialog() { KillTimeCallback(PULSE_ID); }

    virtual void Paint(Draw& w) override { w.DrawRect(GetSize(), AppBackground()); }

    virtual void Layout() override
    {
        Rect rc = GetSize();
        int margin = DPI(16), gap = DPI(7), y = margin;
        int w = max(0, rc.GetWidth() - margin * 2);
        context_.SetRect(margin, y, w, DPI(48)); y += DPI(52);
        summary_.SetRect(margin, y, w, DPI(24)); y += DPI(28);
        detail_.SetRect(margin, y, w, DPI(54)); y += DPI(60);

        int button_h = DPI(38), cancel_w = DPI(96), action_w = DPI(190);
        int button_y = max(y, rc.GetHeight() - margin - button_h);
        list_.SetRect(margin, y, w, max(0, button_y - y - gap));
        cancel_.SetRect(max(margin, rc.GetWidth() - margin - cancel_w), button_y, cancel_w, button_h);
        action_.SetRect(max(margin, rc.GetWidth() - margin - cancel_w - gap - action_w), button_y, action_w, button_h);
    }

    int Choose() { return Execute(); }

private:
    void ApplyConfirmStyle()
    {
        UiButton::Style s = UiTheme::ResolveButton(APP_THEME, APP_MODE, UiButtonRole::Accent);
        Color face = pulse_ ? Color(34, 197, 94) : Color(22, 163, 74);
        Color hot = pulse_ ? Color(74, 222, 128) : Color(34, 197, 94);
        Color frame = pulse_ ? Color(134, 239, 172) : Color(74, 222, 128);
        for(int i = 0; i < 4; ++i) {
            s.palette.face[i] = UiFill::Solid(face);
            s.palette.frame[i] = frame;
            s.palette.ink[i] = White();
            s.palette.icon[i] = White();
        }
        s.palette.face[ST_HOT] = UiFill::Solid(hot);
        s.palette.face[ST_PRESSED] = UiFill::Solid(Color(21, 128, 61));
        s.metrics.frame_enabled = true;
        s.metrics.frame_width = DPI(2);
        s.metrics.radius = DPI(8);
        action_.SetCustomStyle(s);
    }

    void Pulse()
    {
        pulse_ = !pulse_;
        ApplyConfirmStyle();
        SetTimeCallback(650, THISBACK(Pulse), PULSE_ID);
    }

    bool pulse_ = false;
    UiListModel model_;
    UiList list_;
    UiLabel context_, summary_, detail_;
    UiButton action_, cancel_;
};

class PlaylistLabWindow : public TopWindow {
public:
    typedef PlaylistLabWindow CLASSNAME;

    PlaylistLabWindow()
        : spotify_client_(spotify_auth_)
    {
        Title("PlaylistLab");
        Sizeable().Zoomable();
        SetRect(0, 0, DPI(1380), DPI(900));
        SetMinSize(Size(DPI(1060), DPI(720)));

        LoadUiState();
        LoadClientProfiles();
        LoadWorkingState();
        BuildUi();
        ApplyTheme();
        ConnectEvents();
        RefreshProfiles();
        RefreshPlaylistProjection();
        RefreshTargetProjection();
        RefreshWorkingProjection();
        RefreshWorkingName();
        UpdateInspector();
        PostCallback([=] { StartTrackArtworkCache(); });

        if(HasActiveProfile() && spotify_auth_.HasRefreshToken())
            PostCallback([=] { StartLoadPlaylists(false); });
    }

    ~PlaylistLabWindow()
    {
        SaveInspectorNote();
        if(spotify_worker_.IsOpen()) spotify_worker_.Wait();
        if(artwork_worker_.IsOpen()) artwork_worker_.Wait();
        if(track_artwork_worker_.IsOpen()) track_artwork_worker_.Wait();
    }

    virtual void Paint(Draw& w) override { w.DrawRect(GetSize(), AppBackground()); }

    virtual void Close() override
    {
        if(spotify_busy_) {
            last_notice_ = "Finish the current Spotify operation before closing PlaylistLab.";
            UpdateSummary();
            return;
        }
        SaveInspectorNote();
        CommitWorkingName();
        SaveUiState();
        SaveWorkingState();
        SaveClientProfiles();
        TopWindow::Close();
    }

    virtual void Layout() override
    {
        Rect rc = GetSize();
        const int margin = DPI(12), gap = DPI(10), header_h = DPI(64);
        header_.SetRect(margin, margin, max(0, rc.GetWidth() - margin * 2), header_h);

        int top = margin + header_h + gap;
        int h = max(0, rc.GetHeight() - top - margin);
        int w = max(0, rc.GetWidth() - margin * 2);
        int library_w = min(DPI(315), max(DPI(245), w * 22 / 100));
        int inspector_w = min(DPI(310), max(DPI(250), w * 21 / 100));
        int center_w = max(DPI(470), w - library_w - inspector_w - gap * 2);
        int center_x = margin + library_w + gap;

        library_panel_.SetRect(margin, top, library_w, h);
        inspector_panel_.SetRect(center_x + center_w + gap, top, inspector_w, h);
        int spotify_h = max(DPI(260), h * 43 / 100);
        spotify_panel_.SetRect(center_x, top, center_w, spotify_h);
        working_panel_.SetRect(center_x, top + spotify_h + gap, center_w, max(0, h - spotify_h - gap));

        LayoutLibrary();
        LayoutSpotify();
        LayoutWorking();
        LayoutInspector();
    }

private:
    String UiStatePath() const { return ConfigFile("playlistlab.ui.json"); }

    void LoadUiState()
    {
        String text = LoadFile(UiStatePath());
        if(text.IsEmpty())
            return;
        try {
            Value parsed = ParseJSON(text);
            if(!IsValueMap(parsed))
                return;
            ValueMap map = parsed;
            last_playlist_id_ = ValueString(map, "last_playlist_id");
            Value transfer = map["transfer_action"];
            if(!transfer.IsVoid() && !IsNull(transfer))
                transfer_action_ = minmax((int)transfer, (int)TRANSFER_SELECTED, (int)TRANSFER_ALL);
            Value destination = map["destination_action"];
            if(!destination.IsVoid() && !IsNull(destination))
                destination_action_ = minmax((int)destination, (int)DEST_CREATE, (int)DEST_REPLACE);
        }
        catch(...) {}
    }

    void SaveUiState() const
    {
        Json json;
        json("last_playlist_id", last_playlist_id_)
            ("transfer_action", transfer_action_)
            ("destination_action", destination_action_);
        SaveFile(UiStatePath(), ~json);
    }

    void LoadWorkingState()
    {
        String error;
        if(!PlaylistLocalState::LoadWorking(working_document_, working_source_, &error))
            last_notice_ = error;
        if(working_document_.name.IsEmpty())
            working_document_.name = "Working Playlist";
    }

    void SaveWorkingState()
    {
        String error;
        if(!PlaylistLocalState::SaveWorking(working_document_, working_source_, &error) && !error.IsEmpty())
            last_notice_ = error;
    }

    void LoadClientProfiles()
    {
        String error;
        if(!PlaylistLocalState::LoadProfiles(client_profiles_, selected_profile_, &error)) {
            last_notice_ = error;
            client_profiles_.Clear();
            selected_profile_ = -1;
        }
        if(client_profiles_.IsEmpty() && spotify_auth_.HasClientId()) {
            SpotifyClientProfile& profile = client_profiles_.Add();
            profile.name = "Default";
            profile.client_id = spotify_auth_.GetClientId();
            selected_profile_ = 0;
            SaveClientProfiles();
        }
        ApplySelectedProfile(false);
    }

    void SaveClientProfiles()
    {
        String error;
        if(!PlaylistLocalState::SaveProfiles(client_profiles_, selected_profile_, &error) && !error.IsEmpty())
            last_notice_ = error;
    }

    bool HasActiveProfile() const
    {
        return selected_profile_ >= 0 && selected_profile_ < client_profiles_.GetCount();
    }

    void ApplySelectedProfile(bool refresh)
    {
        String next = HasActiveProfile() ? client_profiles_[selected_profile_].client_id : String();
        bool changed = next != spotify_auth_.GetClientId();
        spotify_auth_.SetClientId(next);
        spotify_auth_.Save();
        if(changed) {
            ClearSpotifyView();
            last_playlist_id_.Clear();
            SaveUiState();
        }
        if(refresh && !next.IsEmpty())
            StartLoadPlaylists(false);
    }

    void BuildUi()
    {
        Add(header_); Add(library_panel_); Add(spotify_panel_); Add(working_panel_); Add(inspector_panel_);
        header_.SetTitle("PlaylistLab")
               .SetSubTitle("Spotify Playlist  →  Working Playlist  →  Spotify")
               .ShowTitleLine(false)
               .SetContentInset(DPI(8));

        library_panel_.Add(library_heading_); library_panel_.Add(library_hint_);
        library_panel_.Add(profile_selector_); library_panel_.Add(profile_add_);
        library_panel_.Add(profile_edit_); library_panel_.Add(profile_refresh_); library_panel_.Add(playlist_list_);
        library_heading_.SetText("SPOTIFY PLAYLISTS");
        library_hint_.SetText("Choose a Spotify playlist. Readable tracks can be copied or dragged into Working.");
        profile_add_.SetIcon(ICON_CONTENT_OUTLINED_ADD_48()).SetIconSize(DPI(17), DPI(17));
        profile_edit_.SetIcon(ICON_DESIGN_EDIT_TEXT_48()).SetIconSize(DPI(17), DPI(17));
        profile_refresh_.SetIcon(CtrlImg::redo()).SetIconSize(DPI(17), DPI(17));
        profile_add_.Tip("Add a Spotify Client ID profile.");
        profile_edit_.Tip("Edit the selected profile name and Client ID. Delete is available inside the editor.");
        profile_refresh_.Tip("Authorize if needed and refresh Spotify playlists for this Client ID.");

        spotify_panel_.Add(spotify_heading_); spotify_panel_.Add(spotify_meta_);
        spotify_panel_.Add(spotify_to_working_label_); spotify_panel_.Add(spotify_transfer_);
        spotify_panel_.Add(open_spotify_); spotify_panel_.Add(rename_spotify_); spotify_panel_.Add(target_track_list_);
        spotify_heading_.SetText("SELECTED SPOTIFY PLAYLIST");
        spotify_meta_.SetText("Choose a playlist from the library.");
        spotify_to_working_label_.SetText("Working Playlist");
        spotify_transfer_.Add("Add Selected", TRANSFER_SELECTED);
        spotify_transfer_.Add("Add All", TRANSFER_ALL);
        spotify_transfer_.SetPopupMinWidth(DPI(150));
        SetTransferAction(transfer_action_, false);
        open_spotify_.SetText("Open Spotify");
        rename_spotify_.SetIcon(ICON_DESIGN_EDIT_TEXT_48()).SetIconSize(DPI(16), DPI(16));
        rename_spotify_.Tip("Rename the selected editable Spotify playlist.");
        spotify_transfer_.Tip("Copy tracks from the selected Spotify playlist into Working. The chosen dropdown action becomes the default button action.");

        working_panel_.Add(working_heading_); working_panel_.Add(working_name_);
        working_panel_.Add(destination_label_); working_panel_.Add(destination_);
        working_panel_.Add(find_); working_panel_.Add(import_csv_); working_panel_.Add(paste_text_);
        working_panel_.Add(resolve_unmatched_); working_panel_.Add(export_csv_);
        working_panel_.Add(remove_working_); working_panel_.Add(clear_working_); working_panel_.Add(working_list_);
        working_heading_.SetText("WORKING PLAYLIST");
        working_name_.SetPlaceholder("Working Playlist name");
        destination_label_.SetText("Playlist");
        destination_.Add("Create New", DEST_CREATE);
        destination_.Add("Append Missing", DEST_APPEND);
        destination_.Add("Replace", DEST_REPLACE);
        destination_.SetPopupMinWidth(DPI(180));
        destination_.SetItemDescription(0, "Create a new private Spotify playlist from the exact Working order.");
        destination_.SetItemDescription(1, "Append only missing Working occurrences to the selected editable playlist. No reorder or delete.");
        destination_.SetItemDescription(2, "Destructive: replace the selected editable playlist with the exact Working order.");
        SetDestinationAction(destination_action_, false);
        find_.SetPlaceholder("Find a song or artist…  (Enter)");
        import_csv_.SetText("Import CSV");
        paste_text_.SetText("Paste Text");
        resolve_unmatched_.SetText("Resolve");
        resolve_unmatched_.Tip("Search Spotify for unresolved/missing Working rows. Strong unique matches are linked automatically; ambiguous rows stay amber for review.");
        export_csv_.SetText("Export CSV");
        remove_working_.SetIcon(ICON_CONTENT_OUTLINED_REMOVE_48()).SetIconSize(DPI(16), DPI(16));
        clear_working_.SetIcon(ICON_DESIGN_DELETE_48()).SetIconSize(DPI(16), DPI(16));
        remove_working_.Tip("Remove the selected Working rows.");
        clear_working_.Tip("Clear the local Working Playlist tracks while keeping its name. Spotify is not modified.");
        find_.Tip("Type a title, artist, or both and press Enter. Choose a Spotify result and it is added directly to Working.");

        inspector_panel_.Add(inspector_heading_); inspector_panel_.Add(inspector_art_);
        inspector_panel_.Add(inspector_title_); inspector_panel_.Add(inspector_artist_);
        inspector_panel_.Add(inspector_album_); inspector_panel_.Add(inspector_time_);
        inspector_panel_.Add(inspector_state_); inspector_panel_.Add(play_spotify_);
        inspector_panel_.Add(review_match_); inspector_panel_.Add(notes_heading_); inspector_panel_.Add(notes_);
        inspector_panel_.Add(notice_);
        inspector_heading_.SetText("TRACK DETAILS");
        inspector_title_.SetText("No Working track selected");
        notes_heading_.SetText("Notes");
        play_spotify_.SetText("Play in Spotify");
        review_match_.SetText("Review Match");
        review_match_.Hide();
        review_match_.Tip("Choose the correct Spotify candidate for an ambiguous Working row.");
        notes_.Tip("PlaylistLab-owned notes. These are stored locally with Working and are never sent to Spotify.");

        UiItemRenderStyle track_style = track_renderer_.GetStyle();
        track_style.image_extent = DPI(34);
        track_style.image_radius = DPI(4);
        track_renderer_.SetCustomStyle(track_style);

        playlist_list_.SetModel(playlist_model_)
                      .SetItemRender(playlist_renderer_)
                      .EnableRenameOnDblClick(false)
                      .EnableDragReorder(false)
                      .ShowDragHandle(false);

        target_track_list_.SetModel(target_track_model_)
                          .SetItemRender(track_renderer_)
                          .SetSelectionMode(UILISTSEL_MULTI)
                          .EnableRenameOnDblClick(false)
                          .EnableDragReorder(false)
                          .ShowDragHandle(false);
        target_track_list_.EnableTransferSource();

        working_list_.SetModel(working_model_)
                     .SetItemRender(track_renderer_)
                     .SetSelectionMode(UILISTSEL_MULTI)
                     .EnableRenameOnDblClick(false)
                     .EnableDragReorder(true)
                     .EnableInternalMutation(false)
                     .ShowDragHandle(true)
                     .SetDragSide(UiAlign::RIGHT)
                     .SetDragGlyph(ICON_DESIGN_DRAG_INDICATOR_48());
        working_list_.EnableTransferTarget();
        working_list_.Tip("Green/blue = publishable, amber = review, red = missing, grey = unresolved. Double-click an amber row to review it. Drag the small right grip to reorder.");
    }

    void ApplyTheme()
    {
        header_.SetCustomStyle(UiTheme::ResolveTitleCard(APP_THEME, APP_MODE));
        library_panel_.SetCustomStyle(UiTheme::ResolvePanel(APP_THEME, APP_MODE, UiPanelRole::Subtle));
        spotify_panel_.SetCustomStyle(UiTheme::ResolvePanel(APP_THEME, APP_MODE, UiPanelRole::Surface));
        working_panel_.SetCustomStyle(UiTheme::ResolvePanel(APP_THEME, APP_MODE, UiPanelRole::Strong));
        inspector_panel_.SetCustomStyle(UiTheme::ResolvePanel(APP_THEME, APP_MODE, UiPanelRole::Subtle));

        UiLabel::Style heading = UiTheme::ResolveLabel(APP_THEME, APP_MODE, UiLabelRole::Title);
        heading.font = SansSerifZ(15).Bold();
        heading.metrics.text_font = heading.font;
        heading.metrics.use_text_font = true;
        for(UiLabel *label : { &library_heading_, &spotify_heading_, &working_heading_, &inspector_heading_ })
            label->SetCustomStyle(heading);

        UiLabel::Style body = UiTheme::ResolveLabel(APP_THEME, APP_MODE, UiLabelRole::Body);
        UiLabel::Style caption = UiTheme::ResolveLabel(APP_THEME, APP_MODE, UiLabelRole::Caption);
        spotify_to_working_label_.SetCustomStyle(caption);
        destination_label_.SetCustomStyle(caption);
        notes_heading_.SetCustomStyle(caption);
        for(UiLabel *label : { &library_hint_, &spotify_meta_, &inspector_artist_, &inspector_album_,
                              &inspector_time_, &inspector_state_, &notice_ })
            label->SetCustomStyle(caption);
        inspector_title_.SetCustomStyle(body);

        UiButton::Style standard = UiTheme::ResolveButton(APP_THEME, APP_MODE, UiButtonRole::Standard);
        UiButton::Style subtle = UiTheme::ResolveButton(APP_THEME, APP_MODE, UiButtonRole::Subtle);
        UiButton::Style accent = UiTheme::ResolveButton(APP_THEME, APP_MODE, UiButtonRole::Accent);
        for(UiButton *button : { &profile_add_, &profile_edit_, &profile_refresh_, &rename_spotify_,
                                 &import_csv_, &paste_text_, &resolve_unmatched_, &remove_working_, &clear_working_,
                                 &play_spotify_, &review_match_ })
            button->SetCustomStyle(standard);
        open_spotify_.SetCustomStyle(subtle);
        export_csv_.SetCustomStyle(subtle);
        spotify_transfer_.SetCustomStyle(accent);
        destination_.SetCustomStyle(accent);
        profile_selector_.SetCustomStyle(UiTheme::ResolveDropdown(APP_THEME, APP_MODE));

        UiList::Style playlists = UiTheme::ResolveList(APP_THEME, APP_MODE);
        playlists.row_height = DPI(70);
        playlists.show_checks = false;
        playlists.show_icons = false;
        playlists.show_metadata_marker = false;
        playlists.right_text_as_badge = true;
        playlists.badge_radius = DPI(8);
        playlists.row_radius = DPI(6);
        playlists.item_spacing = DPI(2);
        playlist_list_.SetCustomStyle(playlists);

        UiList::Style source = UiTheme::ResolveList(APP_THEME, APP_MODE);
        source.row_height = DPI(48);
        source.show_checks = false;
        source.show_icons = false;
        source.show_metadata_marker = false;
        source.right_text_as_badge = false;
        target_track_list_.SetCustomStyle(source);

        UiList::Style working = source;
        working.show_metadata_marker = true;
        working.metadata_size = DPI(8);
        working.metadata_gap = DPI(8);
        working.show_drag_handle = true;
        working.drag_side = UiAlign::RIGHT;
        working.drag_size = DPI(16);
        working.drag_gap = DPI(8);
        working.right_text_as_badge = false;
        working_list_.SetCustomStyle(working);
    }

    void ConnectEvents()
    {
        profile_selector_.WhenSelect = [=](int) { OnProfileSelected(); };
        profile_add_.WhenAction = [=] { AddClientProfile(); };
        profile_edit_.WhenAction = [=] { EditClientProfile(); };
        profile_refresh_.WhenAction = [=] { StartLoadPlaylists(true); };
        playlist_list_.WhenSelection = [=] { OnPlaylistSelection(); };

        spotify_transfer_.WhenAction = [=] { RunTransferAction(transfer_action_); };
        spotify_transfer_.WhenSelect = [=](int, const Value& value) {
            int action = IsNull(value) ? TRANSFER_SELECTED : (int)value;
            SetTransferAction(action, true);
            RunTransferAction(action);
        };
        open_spotify_.WhenAction = [=] { OpenTargetInSpotify(); };
        rename_spotify_.WhenAction = [=] { RenameSpotifyPlaylist(); };

        working_name_.WhenAction = [=] { CommitWorkingName(); };
        find_.WhenAction = [=] { StartFindWorking(); };
        import_csv_.WhenAction = [=] { ImportCsvIntoWorking(); };
        paste_text_.WhenAction = [=] { PasteIntoWorking(); };
        resolve_unmatched_.WhenAction = [=] { StartResolveWorkingAll(); };
        export_csv_.WhenAction = [=] { ExportWorking(); };
        remove_working_.WhenAction = [=] { RemoveWorkingSelected(); };
        clear_working_.WhenAction = [=] { ClearWorking(); };

        destination_.WhenAction = [=] { RunDestinationAction(destination_action_); };
        destination_.WhenSelect = [=](int, const Value& value) {
            int action = IsNull(value) ? DEST_CREATE : (int)value;
            SetDestinationAction(action, true);
            RunDestinationAction(action);
        };

        working_list_.WhenSelection = [=] { UpdateInspector(); };
        working_list_.WhenAction = [=] {
            int i = working_list_.GetCursor();
            if(i >= 0 && i < working_document_.tracks.GetCount() && working_document_.tracks[i].state == TRACK_REVIEW)
                ReviewWorkingCandidate();
        };
        working_list_.WhenReorderRequest = [=](UiReorderRequest& request) { ReorderWorking(request); };
        working_list_.WhenTransferDrop = [=](const PlaylistTransferList& source, const Vector<int>& rows) {
            if(&source == &target_track_list_)
                AddSpotifyRows(rows, true);
        };

        review_match_.WhenAction = [=] { ReviewWorkingCandidate(); };
        play_spotify_.WhenAction = [=] { PlaySelectedInSpotify(); };
        notes_.WhenAction = [=] { SaveInspectorNote(); };
    }

    void LayoutLibrary()
    {
        Rect rc = library_panel_.GetSize();
        int margin = DPI(12), gap = DPI(5), w = max(0, rc.GetWidth() - margin * 2), y = margin;
        library_heading_.SetRect(margin, y, w, DPI(26)); y += DPI(30);
        library_hint_.SetRect(margin, y, w, DPI(36)); y += DPI(42);
        int icon = DPI(30);
        int selector_w = max(DPI(120), w - icon * 3 - gap * 3);
        profile_selector_.SetRect(margin, y, selector_w, DPI(32));
        int x = margin + selector_w + gap;
        profile_add_.SetRect(x, y, icon, DPI(32)); x += icon + gap;
        profile_edit_.SetRect(x, y, icon, DPI(32)); x += icon + gap;
        profile_refresh_.SetRect(x, y, icon, DPI(32));
        y += DPI(40);
        playlist_list_.SetRect(margin, y, w, max(0, rc.GetHeight() - y - margin));
    }

    void LayoutSpotify()
    {
        Rect rc = spotify_panel_.GetSize();
        int margin = DPI(12), gap = DPI(6), w = max(0, rc.GetWidth() - margin * 2), y = margin;
        int rename_w = DPI(32), open_w = DPI(104), split_w = DPI(128), label_w = DPI(94);
        int controls = rename_w + open_w + split_w + label_w + gap * 3;
        spotify_heading_.SetRect(margin, y, max(DPI(160), w - controls), DPI(30));
        int x = max(margin, rc.GetWidth() - margin - controls);
        spotify_to_working_label_.SetRect(x, y + DPI(4), label_w, DPI(24)); x += label_w + gap;
        spotify_transfer_.SetRect(x, y, split_w, DPI(32)); x += split_w + gap;
        open_spotify_.SetRect(x, y, open_w, DPI(32)); x += open_w + gap;
        rename_spotify_.SetRect(x, y, rename_w, DPI(32));
        y += DPI(36);
        spotify_meta_.SetRect(margin, y, w, DPI(24)); y += DPI(28);
        target_track_list_.SetRect(margin, y, w, max(0, rc.GetHeight() - y - margin));
    }

    void LayoutWorking()
    {
        Rect rc = working_panel_.GetSize();
        int margin = DPI(12), gap = DPI(6), w = max(0, rc.GetWidth() - margin * 2), y = margin;

        int destination_label_w = DPI(48), destination_w = DPI(132);
        int right = destination_label_w + gap + destination_w;
        working_heading_.SetRect(margin, y, DPI(155), DPI(30));
        int name_x = margin + DPI(160);
        working_name_.SetRect(name_x, y, max(DPI(150), w - DPI(160) - right - gap), DPI(32));
        int dx = rc.GetWidth() - margin - right;
        destination_label_.SetRect(dx, y + DPI(4), destination_label_w, DPI(24));
        destination_.SetRect(dx + destination_label_w + gap, y, destination_w, DPI(32));
        y += DPI(38);

        int icon = DPI(30), import_w = DPI(78), paste_w = DPI(78), resolve_w = DPI(70), export_w = DPI(82);
        int fixed = import_w + paste_w + resolve_w + export_w + icon * 2 + gap * 6;
        int find_w = max(DPI(150), w - fixed);
        int x = margin;
        find_.SetRect(x, y, find_w, DPI(32)); x += find_w + gap;
        import_csv_.SetRect(x, y, import_w, DPI(32)); x += import_w + gap;
        paste_text_.SetRect(x, y, paste_w, DPI(32)); x += paste_w + gap;
        resolve_unmatched_.SetRect(x, y, resolve_w, DPI(32)); x += resolve_w + gap;
        export_csv_.SetRect(x, y, export_w, DPI(32)); x += export_w + gap;
        remove_working_.SetRect(x, y, icon, DPI(32)); x += icon + gap;
        clear_working_.SetRect(x, y, icon, DPI(32));
        y += DPI(40);

        working_list_.SetRect(margin, y, w, max(0, rc.GetHeight() - y - margin));
    }

    void LayoutInspector()
    {
        Rect rc = inspector_panel_.GetSize();
        int margin = DPI(12), w = max(0, rc.GetWidth() - margin * 2), y = margin;
        inspector_heading_.SetRect(margin, y, w, DPI(30)); y += DPI(36);
        int art = min(DPI(108), w);
        inspector_art_.SetRect(margin, y, art, art); y += art + DPI(10);
        inspector_title_.SetRect(margin, y, w, DPI(42)); y += DPI(44);
        inspector_artist_.SetRect(margin, y, w, DPI(22)); y += DPI(22);
        inspector_album_.SetRect(margin, y, w, DPI(22)); y += DPI(22);
        inspector_time_.SetRect(margin, y, w, DPI(22)); y += DPI(22);
        inspector_state_.SetRect(margin, y, w, DPI(34)); y += DPI(38);
        if(review_match_.IsShown()) {
            int half = max(0, (w - DPI(6)) / 2);
            play_spotify_.SetRect(margin, y, half, DPI(32));
            review_match_.SetRect(margin + half + DPI(6), y, half, DPI(32));
        }
        else
            play_spotify_.SetRect(margin, y, w, DPI(32));
        y += DPI(40);
        notes_heading_.SetRect(margin, y, w, DPI(22)); y += DPI(24);
        int notice_h = DPI(70);
        notes_.SetRect(margin, y, w, max(DPI(100), rc.GetHeight() - y - margin - notice_h - DPI(8)));
        notice_.SetRect(margin, rc.GetHeight() - margin - notice_h, w, notice_h);
    }

    bool ProfileClientIdExists(const String& client_id, int except = -1) const
    {
        for(int i = 0; i < client_profiles_.GetCount(); ++i)
            if(i != except && client_profiles_[i].client_id == client_id)
                return true;
        return false;
    }

    void RefreshProfiles()
    {
        rebuilding_profiles_ = true;
        profile_selector_.Clear();
        for(int i = 0; i < client_profiles_.GetCount(); ++i)
            profile_selector_.Add(ProfileDisplay(client_profiles_[i]), i);
        if(HasActiveProfile())
            profile_selector_.Select(selected_profile_);
        rebuilding_profiles_ = false;
        UpdateActionState();
    }

    void AddClientProfile()
    {
        if(spotify_busy_)
            return;
        ProfileDialog dialog("Add Spotify Client Profile", String(), String(), false);
        if(dialog.Execute() != IDOK)
            return;
        String name = dialog.GetName();
        String client_id = dialog.GetClientId();
        if(client_id.IsEmpty() || ProfileClientIdExists(client_id)) {
            Exclamation("That Spotify Client ID is empty or already stored.");
            return;
        }
        SpotifyClientProfile& profile = client_profiles_.Add();
        profile.name = name.IsEmpty() ? Format("Spotify Client %d", client_profiles_.GetCount()) : name;
        profile.client_id = client_id;
        selected_profile_ = client_profiles_.GetCount() - 1;
        SaveClientProfiles();
        RefreshProfiles();
        ApplySelectedProfile(true);
    }

    void EditClientProfile()
    {
        if(spotify_busy_ || !HasActiveProfile())
            return;
        const SpotifyClientProfile& current = client_profiles_[selected_profile_];
        ProfileDialog dialog("Edit Spotify Client Profile", current.name, current.client_id, true);
        int result = dialog.Execute();
        if(result == ProfileDialog::PROFILE_DELETE) {
            DeleteClientProfile();
            return;
        }
        if(result != IDOK)
            return;
        String name = dialog.GetName();
        String client_id = dialog.GetClientId();
        if(client_id.IsEmpty() || ProfileClientIdExists(client_id, selected_profile_)) {
            Exclamation("That Spotify Client ID is empty or already stored.");
            return;
        }
        client_profiles_[selected_profile_].name = name.IsEmpty() ? "Spotify Client" : name;
        client_profiles_[selected_profile_].client_id = client_id;
        SaveClientProfiles();
        RefreshProfiles();
        ApplySelectedProfile(true);
    }

    void DeleteClientProfile()
    {
        if(!HasActiveProfile())
            return;
        String name = client_profiles_[selected_profile_].name;
        if(!PromptYesNo("Delete stored Spotify Client ID profile '" + name + "'?\n\nNo Client Secret is stored by PlaylistLab."))
            return;
        int removed = selected_profile_;
        client_profiles_.Remove(removed);
        selected_profile_ = client_profiles_.IsEmpty() ? -1 : min(removed, client_profiles_.GetCount() - 1);
        SaveClientProfiles();
        RefreshProfiles();
        ApplySelectedProfile(false);
        if(HasActiveProfile())
            StartLoadPlaylists(false);
        else {
            spotify_auth_.SetClientId(String());
            spotify_auth_.Disconnect();
            ClearSpotifyView();
            last_notice_ = "No Spotify Client ID profile selected.";
            UpdateSummary();
        }
    }

    void OnProfileSelected()
    {
        if(rebuilding_profiles_ || spotify_busy_)
            return;
        int selected = profile_selector_.GetSelection();
        if(selected < 0 || selected >= client_profiles_.GetCount() || selected == selected_profile_)
            return;
        selected_profile_ = selected;
        SaveClientProfiles();
        ApplySelectedProfile(true);
    }

    void SetTransferAction(int action, bool persist)
    {
        transfer_action_ = minmax(action, (int)TRANSFER_SELECTED, (int)TRANSFER_ALL);
        spotify_transfer_.SetText(TransferText(transfer_action_));
        if(persist)
            SaveUiState();
    }

    void SetDestinationAction(int action, bool persist)
    {
        destination_action_ = minmax(action, (int)DEST_CREATE, (int)DEST_REPLACE);
        destination_.SetText(DestinationText(destination_action_));
        if(persist)
            SaveUiState();
    }

    void CommitWorkingName()
    {
        String name = TrimBoth(working_name_.GetTextUtf8());
        if(name.IsEmpty())
            name = "Working Playlist";
        if(working_document_.name != name) {
            working_document_.name = name;
            working_document_.dirty = true;
            SaveWorkingState();
            last_notice_ = "Working Playlist renamed locally.";
            UpdateSummary();
        }
        RefreshWorkingName();
    }

    void RefreshWorkingName()
    {
        updating_working_name_ = true;
        working_name_.SetTextUtf8(working_document_.name.IsEmpty() ? String("Working Playlist") : working_document_.name);
        updating_working_name_ = false;
    }

    void MergeWorkingSource(const String& source)
    {
        if(working_document_.tracks.IsEmpty() || working_source_.IsEmpty())
            working_source_ = source;
        else if(working_source_ != source && working_source_ != "Mixed sources")
            working_source_ = "Mixed sources";
    }

    Image GetTrackArtwork(const SpotifyTrack& track)
    {
        String key = TrackArtworkKey(track);
        if(key.IsEmpty())
            return Image();
        int q = track_images_.Find(key);
        if(q >= 0)
            return track_images_[q];
        Image image = SpotifyImageCache::Load("track-" + key);
        if(!IsNull(image))
            track_images_.Add(key, image);
        return image;
    }

    void QueueTrackArtwork(const SpotifyTrack *track, Vector<String>& keys, Vector<String>& urls)
    {
        if(!track || track->image_url.IsEmpty())
            return;
        String key = TrackArtworkKey(*track);
        if(key.IsEmpty() || track_images_.Find(key) >= 0 || track_artwork_attempted_.Find(key) >= 0 || FindIndex(keys, key) >= 0)
            return;
        Image cached = SpotifyImageCache::Load("track-" + key);
        if(!IsNull(cached)) {
            track_images_.Add(key, cached);
            return;
        }
        if(keys.GetCount() >= 24)
            return;
        track_artwork_attempted_.FindAdd(key);
        keys.Add(key);
        urls.Add(track->image_url);
    }

    void StartTrackArtworkCache()
    {
        if(track_artwork_worker_.IsOpen())
            return;
        track_artwork_job_keys_.Clear();
        track_artwork_job_urls_.Clear();
        for(const SpotifyTrack& track : target_tracks_)
            QueueTrackArtwork(&track, track_artwork_job_keys_, track_artwork_job_urls_);
        for(const TrackEntry& entry : working_document_.tracks)
            QueueTrackArtwork(ArtworkTrack(entry), track_artwork_job_keys_, track_artwork_job_urls_);
        if(track_artwork_job_keys_.IsEmpty())
            return;
        if(!track_artwork_worker_.Run([=] {
            Vector<Image> images;
            images.SetCount(track_artwork_job_keys_.GetCount());
            for(int i = 0; i < track_artwork_job_keys_.GetCount(); ++i) {
                String ignored;
                images[i] = SpotifyImageCache::LoadOrFetch("track-" + track_artwork_job_keys_[i],
                                                           track_artwork_job_urls_[i], &ignored);
            }
            {
                GuiLock __;
                track_artwork_result_images_ = pick(images);
            }
            PostCallback([=] { FinishTrackArtworkCache(); });
        })) {
            track_artwork_job_keys_.Clear();
            track_artwork_job_urls_.Clear();
            track_artwork_attempted_.Clear();
            last_notice_ = "PlaylistLab could not start the track artwork cache worker.";
        }
    }

    void FinishTrackArtworkCache()
    {
        if(track_artwork_worker_.IsOpen())
            track_artwork_worker_.Wait();
        for(int i = 0; i < track_artwork_job_keys_.GetCount() && i < track_artwork_result_images_.GetCount(); ++i)
            if(!IsNull(track_artwork_result_images_[i])) {
                int q = track_images_.Find(track_artwork_job_keys_[i]);
                if(q < 0) track_images_.Add(track_artwork_job_keys_[i], track_artwork_result_images_[i]);
                else track_images_[q] = track_artwork_result_images_[i];
            }
        track_artwork_result_images_.Clear();
        track_artwork_job_keys_.Clear();
        track_artwork_job_urls_.Clear();
        RefreshTargetProjection();
        RefreshWorkingProjection(working_list_.GetCursor());
        StartTrackArtworkCache();
    }

    void RefreshPlaylistProjection(int selected = -1)
    {
        Vector<UiModelItem> rows;
        rows.Reserve(spotify_playlists_.GetCount());
        for(int i = 0; i < spotify_playlists_.GetCount(); ++i) {
            const SpotifyPlaylistInfo& playlist = spotify_playlists_[i];
            UiModelItem row;
            row.text = playlist.name.IsEmpty() ? String("Untitled Spotify playlist") : playlist.name;
            String owner = playlist.owner_name.IsEmpty() ? playlist.owner_id : playlist.owner_name;
            row.description = owner.IsEmpty() ? String("Spotify playlist") : "by " + owner;
            String access = playlist.items_accessible ? (playlist.editable ? "EDIT" : "SOURCE") : "META";
            row.right_text = Format("%d  %s", playlist.item_count, access);
            row.data = i;
            if(i < playlist_images_.GetCount()) row.image = playlist_images_[i];
            rows.Add(pick(row));
        }
        rebuilding_playlist_model_ = true;
        playlist_model_.Clear();
        if(!rows.IsEmpty()) playlist_model_.AddRange(rows);
        if(selected >= 0 && selected < spotify_playlists_.GetCount()) playlist_list_.SetCursor(selected);
        rebuilding_playlist_model_ = false;
        UpdateSummary();
    }

    void RefreshTargetProjection()
    {
        Vector<UiModelItem> rows;
        rows.Reserve(target_tracks_.GetCount());
        for(int i = 0; i < target_tracks_.GetCount(); ++i) {
            const SpotifyTrack& track = target_tracks_[i];
            UiModelItem row;
            row.text = Format("%d. %s", i + 1, track.title.IsEmpty() ? String("Untitled Spotify item") : track.title);
            row.description = SpotifyTrackDescription(track);
            row.right_text = track.placeholder ? String("Unavailable") : DurationText(track.duration_ms);
            row.data = i;
            row.image = GetTrackArtwork(track);
            rows.Add(pick(row));
        }
        target_track_model_.Clear();
        if(!rows.IsEmpty()) target_track_model_.AddRange(rows);
        UpdateSummary();
    }

    void RefreshWorkingProjection(int selected = -1)
    {
        Vector<UiModelItem> rows;
        rows.Reserve(working_document_.tracks.GetCount());
        for(int i = 0; i < working_document_.tracks.GetCount(); ++i) {
            const TrackEntry& entry = working_document_.tracks[i];
            UiModelItem row;
            row.text = Format("%d. %s", i + 1, TrackDisplayTitle(entry));
            row.description = TrackDisplayDescription(entry);
            if(const SpotifyTrack *track = ArtworkTrack(entry)) {
                row.image = GetTrackArtwork(*track);
                row.right_text = DurationText(track->duration_ms);
            }
            row.data = i;
            row.has_metadata = true;
            row.metadata_color = MatchStateColor(entry.state);
            rows.Add(pick(row));
        }
        working_model_.Clear();
        if(!rows.IsEmpty()) working_model_.AddRange(rows);
        if(selected >= 0 && selected < working_document_.tracks.GetCount()) working_list_.SetCursor(selected);
        UpdateInspector();
        UpdateSummary();
    }

    void UpdateInspector()
    {
        SaveInspectorNote();
        int index = working_list_.GetCursor();
        inspector_index_ = index;
        updating_inspector_ = true;
        if(index < 0 || index >= working_document_.tracks.GetCount()) {
            inspector_art_.SetImage(Image());
            inspector_title_.SetText("No Working track selected");
            inspector_artist_.SetText(Null);
            inspector_album_.SetText(Null);
            inspector_time_.SetText(Null);
            inspector_state_.SetText(Null);
            notes_.SetTextUtf8(String());
            updating_inspector_ = false;
            UpdateActionState();
            return;
        }
        const TrackEntry& entry = working_document_.tracks[index];
        const SpotifyTrack *track = ArtworkTrack(entry);
        Image art = track ? GetTrackArtwork(*track) : Image();
        if(!IsNull(art) && art.GetSize() != Size(DPI(108), DPI(108)))
            art = Rescale(art, DPI(108), DPI(108));
        inspector_art_.SetImage(art);
        inspector_title_.SetText(TrackDisplayTitle(entry));
        inspector_artist_.SetText(entry.ResolvedArtist().IsEmpty() ? "Artist: —" : "Artist: " + entry.ResolvedArtist());
        String album = track ? track->album : entry.requested_album;
        inspector_album_.SetText(album.IsEmpty() ? "Album: —" : "Album: " + album);
        inspector_time_.SetText(track && track->duration_ms > 0 ? "Time: " + DurationText(track->duration_ms) : "Time: —");
        String state = "Match: " + TrackMatchStateText(entry.state);
        if(!entry.note.IsEmpty()) state << "  •  " << entry.note;
        inspector_state_.SetText(state);
        notes_.SetTextUtf8(entry.user_note);
        updating_inspector_ = false;
        UpdateActionState();
    }

    void SaveInspectorNote()
    {
        if(updating_inspector_ || inspector_index_ < 0 || inspector_index_ >= working_document_.tracks.GetCount())
            return;
        String note = notes_.GetTextUtf8();
        if(working_document_.tracks[inspector_index_].user_note == note)
            return;
        working_document_.tracks[inspector_index_].user_note = note;
        working_document_.dirty = true;
        SaveWorkingState();
    }

    void UpdateSummary()
    {
        int working_total = working_document_.tracks.GetCount();
        int resolved = working_document_.GetResolvedCount();
        int review = working_document_.GetReviewCount();
        int missing = working_document_.GetMissingCount();
        String subtitle = Format("%d Spotify playlists  •  %d Working tracks  •  %d publishable",
                                 spotify_playlists_.GetCount(), working_total, resolved);
        if(!target_playlist_name_.IsEmpty())
            subtitle << "  •  selected: " << target_playlist_name_;
        if(spotify_busy_)
            subtitle << "  •  Spotify working";
        header_.SetSubTitle(subtitle);

        if(target_playlist_id_.IsEmpty())
            spotify_meta_.SetText("Choose a Spotify playlist from the library.");
        else if(!target_items_accessible_)
            spotify_meta_.SetText(target_playlist_name_ + "  •  metadata only  •  Spotify refused item access for this account/app.");
        else if(!target_loaded_)
            spotify_meta_.SetText("Loading " + target_playlist_name_ + "…");
        else
            spotify_meta_.SetText(Format("%s  •  %d items  •  %s",
                                         target_playlist_name_, target_tracks_.GetCount(),
                                         target_editable_ ? "editable" : "read-only source"));

        String notice = last_notice_;
        if(notice.IsEmpty())
            notice = Format("Working: %d tracks / %d publishable / %d review / %d missing. Spotify writes always show an exact preview first.",
                            working_total, resolved, review, missing);
        notice_.SetText(notice);
        UpdateActionState();
    }

    void UpdateActionState()
    {
        bool idle = !spotify_busy_;
        bool has_profile = HasActiveProfile();
        bool source_ready = idle && target_loaded_ && !target_tracks_.IsEmpty();
        bool editable_target = idle && target_loaded_ && target_editable_ && !target_snapshot_id_.IsEmpty();
        int wi = working_list_.GetCursor();
        const TrackEntry *entry = wi >= 0 && wi < working_document_.tracks.GetCount() ? &working_document_.tracks[wi] : nullptr;
        bool reviewable = idle && entry && entry->state == TRACK_REVIEW && !entry->candidates.IsEmpty();

        profile_selector_.Enable(idle && !client_profiles_.IsEmpty());
        profile_add_.Enable(idle);
        profile_edit_.Enable(idle && has_profile);
        profile_refresh_.Enable(idle && has_profile);
        playlist_list_.Enable(idle && !spotify_playlists_.IsEmpty());
        target_track_list_.Enable(source_ready);
        spotify_transfer_.Enable(source_ready);
        open_spotify_.Enable(idle && !target_spotify_url_.IsEmpty());
        rename_spotify_.Enable(editable_target);

        working_name_.Enable(idle);
        find_.Enable(idle && has_profile);
        import_csv_.Enable(idle);
        paste_text_.Enable(idle);
        resolve_unmatched_.Enable(idle && has_profile && !working_document_.tracks.IsEmpty() && working_document_.GetResolvedCount() < working_document_.tracks.GetCount());
        export_csv_.Enable(idle && !working_document_.tracks.IsEmpty());
        remove_working_.Enable(idle && working_list_.GetSelectionCount() > 0);
        clear_working_.Enable(idle && !working_document_.tracks.IsEmpty());
        working_list_.Enable(idle);
        destination_.Enable(idle && has_profile && !working_document_.tracks.IsEmpty());
        if(destination_.GetCount() >= 3) {
            destination_.SetItemEnabled(0, idle && has_profile && !working_document_.tracks.IsEmpty());
            destination_.SetItemEnabled(1, editable_target && !working_document_.tracks.IsEmpty());
            destination_.SetItemEnabled(2, editable_target && !working_document_.tracks.IsEmpty());
        }
        play_spotify_.Enable(idle && entry && ArtworkTrack(*entry) && !ArtworkTrack(*entry)->spotify_url.IsEmpty());
        review_match_.Show(reviewable);
        review_match_.Enable(reviewable);
        notes_.Enable(idle && entry);
        LayoutInspector();
    }

    bool PrepareSpotifyWorker()
    {
        if(spotify_busy_)
            return false;
        if(spotify_worker_.IsOpen())
            spotify_worker_.Wait();
        return true;
    }

    bool EnsureSpotifyAuthorizedWorker(String& error)
    {
        if(!HasActiveProfile() || !spotify_auth_.HasClientId()) {
            error = "Choose or add a Spotify Client ID profile first.";
            return false;
        }
        if(!spotify_auth_.HasAccessToken() && !spotify_auth_.HasRefreshToken()) {
            if(!spotify_auth_.AuthorizeInteractive()) {
                error = spotify_auth_.GetLastError();
                return false;
            }
        }
        if(!spotify_auth_.EnsureAccessToken()) {
            error = spotify_auth_.GetLastError();
            return false;
        }
        return true;
    }

    void SetSpotifyBusy(bool busy, const String& notice = String())
    {
        spotify_busy_ = busy;
        if(!notice.IsEmpty()) last_notice_ = notice;
        UpdateSummary();
    }

    void StartLoadPlaylists(bool require_profile)
    {
        if(require_profile && !HasActiveProfile()) {
            Exclamation("Add or choose a Spotify Client ID profile first.");
            return;
        }
        if(!HasActiveProfile() || !PrepareSpotifyWorker())
            return;
        SetSpotifyBusy(true, "Connecting to Spotify and loading playlists…");
        if(!spotify_worker_.Run([=] {
            Vector<SpotifyPlaylistInfo> found;
            String error;
            bool ok = EnsureSpotifyAuthorizedWorker(error);
            if(ok && !spotify_client_.GetPlaylists(found)) {
                error = spotify_client_.GetLastError();
                ok = false;
            }
            {
                GuiLock __;
                spotify_playlists_ = pick(found);
                pending_spotify_ok_ = ok;
                pending_spotify_error_ = error;
            }
            PostCallback([=] { FinishLoadPlaylists(); });
        }))
            SetSpotifyBusy(false, "PlaylistLab could not start the Spotify worker thread.");
    }

    void FinishLoadPlaylists()
    {
        bool ok = pending_spotify_ok_;
        String error = pending_spotify_error_;
        SetSpotifyBusy(false);
        if(!ok) {
            last_notice_ = error.IsEmpty() ? "Spotify playlist loading failed." : error;
            UpdateSummary();
            Exclamation(last_notice_);
            return;
        }
        for(SpotifyPlaylistInfo& playlist : spotify_playlists_)
            playlist.items_accessible = true;
        playlist_images_.SetCount(spotify_playlists_.GetCount());
        for(int i = 0; i < spotify_playlists_.GetCount(); ++i)
            playlist_images_[i] = SpotifyImageCache::Load("playlist-" + spotify_playlists_[i].id);
        int selected = FindPlaylist(last_playlist_id_);
        if(selected < 0 && !spotify_playlists_.IsEmpty()) selected = 0;
        RefreshPlaylistProjection(selected);
        StartArtworkCache();
        if(spotify_playlists_.IsEmpty()) {
            ClearTarget();
            RefreshTargetProjection();
            last_notice_ = "Spotify returned no playlists for this account.";
            UpdateSummary();
            return;
        }
        last_notice_ = Format("Loaded %d Spotify playlist%s.", spotify_playlists_.GetCount(), spotify_playlists_.GetCount() == 1 ? "" : "s");
        UpdateSummary();
        if(selected >= 0) StartLoadTarget(selected);
    }

    int FindPlaylist(const String& id) const
    {
        for(int i = 0; i < spotify_playlists_.GetCount(); ++i)
            if(spotify_playlists_[i].id == id) return i;
        return -1;
    }

    void StartArtworkCache()
    {
        if(artwork_worker_.IsOpen()) artwork_worker_.Wait();
        artwork_job_ids_.Clear(); artwork_job_urls_.Clear();
        const int max_fetch = 16;
        for(int i = 0; i < spotify_playlists_.GetCount() && artwork_job_ids_.GetCount() < max_fetch; ++i) {
            if(i < playlist_images_.GetCount() && !IsNull(playlist_images_[i])) continue;
            if(spotify_playlists_[i].image_url.IsEmpty()) continue;
            artwork_job_ids_.Add(spotify_playlists_[i].id);
            artwork_job_urls_.Add(spotify_playlists_[i].image_url);
        }
        if(artwork_job_ids_.IsEmpty()) return;
        if(!artwork_worker_.Run([=] {
            Vector<Image> images;
            images.SetCount(artwork_job_ids_.GetCount());
            for(int i = 0; i < artwork_job_ids_.GetCount(); ++i) {
                String ignored;
                images[i] = SpotifyImageCache::LoadOrFetch("playlist-" + artwork_job_ids_[i], artwork_job_urls_[i], &ignored);
            }
            {
                GuiLock __;
                artwork_result_images_ = pick(images);
            }
            PostCallback([=] { FinishArtworkCache(); });
        })) {
            artwork_job_ids_.Clear();
            artwork_job_urls_.Clear();
            last_notice_ = "PlaylistLab could not start the playlist artwork worker.";
        }
    }

    void FinishArtworkCache()
    {
        if(artwork_worker_.IsOpen()) artwork_worker_.Wait();
        for(int i = 0; i < artwork_job_ids_.GetCount() && i < artwork_result_images_.GetCount(); ++i) {
            if(IsNull(artwork_result_images_[i])) continue;
            int q = FindPlaylist(artwork_job_ids_[i]);
            if(q >= 0 && q < playlist_images_.GetCount()) playlist_images_[q] = artwork_result_images_[i];
        }
        artwork_result_images_.Clear();
        artwork_job_ids_.Clear();
        artwork_job_urls_.Clear();
        RefreshPlaylistProjection(playlist_list_.GetCursor());
        StartArtworkCache();
    }

    void OnPlaylistSelection()
    {
        if(rebuilding_playlist_model_ || spotify_busy_) return;
        int index = playlist_list_.GetCursor();
        if(index < 0 || index >= spotify_playlists_.GetCount()) return;
        if(target_playlist_id_ == spotify_playlists_[index].id && (target_loaded_ || !target_items_accessible_)) return;
        StartLoadTarget(index);
    }

    void ClearSpotifyView()
    {
        if(artwork_worker_.IsOpen()) artwork_worker_.Wait();
        spotify_playlists_.Clear(); playlist_images_.Clear(); playlist_model_.Clear();
        ClearTarget(); RefreshTargetProjection();
    }

    void ClearTarget()
    {
        target_playlist_id_.Clear(); target_playlist_name_.Clear(); target_snapshot_id_.Clear(); target_spotify_url_.Clear();
        target_editable_ = false; target_items_accessible_ = false; target_loaded_ = false;
        target_tracks_.Clear(); target_uris_.Clear();
    }

    void StartLoadTarget(int playlist_index)
    {
        if(playlist_index < 0 || playlist_index >= spotify_playlists_.GetCount()) return;
        const SpotifyPlaylistInfo& playlist = spotify_playlists_[playlist_index];
        target_playlist_id_ = playlist.id;
        target_playlist_name_ = playlist.name;
        target_spotify_url_ = playlist.spotify_url;
        target_editable_ = playlist.editable;
        target_items_accessible_ = playlist.items_accessible;
        target_snapshot_id_.Clear(); target_loaded_ = false; target_tracks_.Clear(); target_uris_.Clear();
        last_playlist_id_ = target_playlist_id_;
        SaveUiState();
        RefreshTargetProjection();
        if(!target_items_accessible_) {
            last_notice_ = "Spotify previously refused item access for this playlist. Refresh playlists to retry its access state.";
            UpdateSummary();
            return;
        }
        if(!PrepareSpotifyWorker()) return;
        String playlist_id = target_playlist_id_;
        SetSpotifyBusy(true, "Loading '" + target_playlist_name_ + "' from Spotify…");
        if(!spotify_worker_.Run([=] {
            Vector<SpotifyTrack> found;
            String snapshot;
            bool ok = spotify_client_.GetPlaylistItems(playlist_id, found, &snapshot);
            String error = ok ? String() : spotify_client_.GetLastError();
            int status = ok ? 0 : spotify_client_.GetLastStatus();
            {
                GuiLock __;
                target_tracks_ = pick(found);
                target_snapshot_id_ = snapshot;
                pending_spotify_ok_ = ok;
                pending_spotify_error_ = error;
                pending_spotify_status_ = status;
            }
            PostCallback([=] { FinishLoadTarget(); });
        }))
            SetSpotifyBusy(false, "PlaylistLab could not start the Spotify worker thread.");
    }

    void FinishLoadTarget()
    {
        bool ok = pending_spotify_ok_;
        String error = pending_spotify_error_;
        int status = pending_spotify_status_;
        SetSpotifyBusy(false);
        int q = FindPlaylist(target_playlist_id_);
        if(!ok) {
            target_loaded_ = false;
            if(status == 403 || status == 404) {
                target_items_accessible_ = false;
                if(q >= 0) spotify_playlists_[q].items_accessible = false;
                last_notice_ = "Spotify exposes this playlist's metadata but refused its item list for the current account/app.";
            }
            else last_notice_ = error.IsEmpty() ? "Spotify playlist loading failed." : error;
            RefreshTargetProjection(); RefreshPlaylistProjection(q); Exclamation(last_notice_); return;
        }
        target_uris_.Clear(); target_uris_.Reserve(target_tracks_.GetCount());
        for(const SpotifyTrack& track : target_tracks_) target_uris_.Add(track.uri);
        target_items_accessible_ = true; target_loaded_ = true;
        if(q >= 0) {
            spotify_playlists_[q].items_accessible = true;
            spotify_playlists_[q].item_count = target_tracks_.GetCount();
        }
        last_notice_ = Format("Loaded '%s' with %d item%s.", target_playlist_name_, target_tracks_.GetCount(), target_tracks_.GetCount() == 1 ? "" : "s");
        RefreshTargetProjection(); RefreshPlaylistProjection(q); StartTrackArtworkCache();
    }

    void OpenTargetInSpotify()
    {
        if(!target_spotify_url_.IsEmpty()) LaunchWebBrowser(target_spotify_url_);
    }

    void RenameSpotifyPlaylist()
    {
        if(!target_loaded_ || !target_editable_) return;
        String name = target_playlist_name_;
        if(!EditTextNotNull(name, "Rename Spotify Playlist", "Playlist name")) return;
        name = TrimBoth(name);
        if(name.IsEmpty() || name == target_playlist_name_) return;
        if(!PrepareSpotifyWorker()) return;
        pending_rename_name_ = name;
        String id = target_playlist_id_;
        SetSpotifyBusy(true, "Renaming Spotify playlist…");
        if(!spotify_worker_.Run([=] {
            bool ok = spotify_client_.UpdatePlaylistDetails(id, pending_rename_name_);
            String error = ok ? String() : spotify_client_.GetLastError();
            {
                GuiLock __;
                pending_spotify_ok_ = ok;
                pending_spotify_error_ = error;
            }
            PostCallback([=] { FinishRenameSpotifyPlaylist(); });
        })) {
            pending_rename_name_.Clear();
            SetSpotifyBusy(false, "PlaylistLab could not start the rename worker.");
        }
    }

    void FinishRenameSpotifyPlaylist()
    {
        bool ok = pending_spotify_ok_;
        String error = pending_spotify_error_;
        SetSpotifyBusy(false);
        if(!ok) {
            last_notice_ = error.IsEmpty() ? "Spotify playlist rename failed." : error;
            UpdateSummary(); Exclamation(last_notice_); return;
        }
        target_playlist_name_ = pending_rename_name_;
        int q = FindPlaylist(target_playlist_id_);
        if(q >= 0) spotify_playlists_[q].name = target_playlist_name_;
        pending_rename_name_.Clear();
        RefreshPlaylistProjection(q);
        last_notice_ = "Spotify playlist renamed.";
        UpdateSummary();
    }

    void RunTransferAction(int action)
    {
        if(action == TRANSFER_ALL) AddSpotifyAll();
        else AddSpotifySelected();
    }

    void AddSpotifyRows(const Vector<int>& rows, bool from_drag)
    {
        if(rows.IsEmpty()) return;
        bool was_empty = working_document_.tracks.IsEmpty();
        MergeWorkingSource("Spotify: " + target_playlist_name_);
        int added = 0;
        for(int row : rows)
            if(row >= 0 && row < target_tracks_.GetCount()) {
                working_document_.tracks.Add(EntryFromSpotify(target_tracks_[row]));
                added++;
            }
        if(added <= 0) return;
        if(was_empty && working_document_.name == "Working Playlist" && !target_playlist_name_.IsEmpty()) {
            working_document_.name = target_playlist_name_;
            RefreshWorkingName();
        }
        working_document_.dirty = true;
        SaveWorkingState();
        last_notice_ = Format(from_drag ? "Dragged %d Spotify track%s into Working." : "Added %d Spotify track%s to Working.",
                              added, added == 1 ? "" : "s");
        RefreshWorkingProjection(working_document_.tracks.GetCount() - 1);
        StartTrackArtworkCache();
    }

    void AddSpotifySelected()
    {
        Vector<int> rows = SelectedIndices(target_track_list_);
        if(rows.IsEmpty()) {
            last_notice_ = "Select one or more Spotify tracks first.";
            UpdateSummary(); return;
        }
        AddSpotifyRows(rows, false);
    }

    void AddSpotifyAll()
    {
        if(!target_loaded_ || target_tracks_.IsEmpty()) return;
        Vector<int> rows;
        for(int i = 0; i < target_tracks_.GetCount(); ++i) rows.Add(i);
        AddSpotifyRows(rows, false);
    }

    void AppendImportedDocument(PlaylistDocument& incoming, const String& source)
    {
        bool was_empty = working_document_.tracks.IsEmpty();
        MergeWorkingSource(source);
        int added = incoming.tracks.GetCount();
        for(TrackEntry& entry : incoming.tracks)
            working_document_.tracks.Add(pick(entry));
        if(was_empty && !incoming.name.IsEmpty()) {
            working_document_.name = incoming.name;
            RefreshWorkingName();
        }
        working_document_.dirty = working_document_.dirty || added > 0;
        SaveWorkingState();
        last_notice_ = Format("Added %d imported track%s directly to Working. Press Resolve when you want Spotify matching.",
                              added, added == 1 ? "" : "s");
        RefreshWorkingProjection(working_document_.tracks.IsEmpty() ? -1 : working_document_.tracks.GetCount() - 1);
    }

    void ImportCsvIntoWorking()
    {
        String path = SelectFileOpen("CSV files\t*.csv\nAll files\t*.*");
        if(path.IsEmpty()) return;
        if(!FileExists(path)) { Exclamation("The selected CSV file could not be opened."); return; }
        PlaylistImportResult result = ImportPlaylistCsv(LoadFile(path), path);
        int warnings = result.warnings.GetCount();
        AppendImportedDocument(result.document, "CSV: " + GetFileName(path));
        if(warnings) {
            last_notice_ = Format("CSV added to Working with %d warning%s; press Resolve when ready to match unresolved rows.", warnings, warnings == 1 ? "" : "s");
            UpdateSummary();
        }
    }

    void PasteIntoWorking()
    {
        String text = ReadClipboardText();
        if(TrimBoth(text).IsEmpty()) { Exclamation("The clipboard does not contain a song list."); return; }
        PlaylistImportResult result = ImportPlaylistText(text, "Clipboard");
        int warnings = result.warnings.GetCount();
        AppendImportedDocument(result.document, "Clipboard");
        if(warnings) {
            last_notice_ = Format("Clipboard added to Working with %d warning%s; press Resolve when ready.", warnings, warnings == 1 ? "" : "s");
            UpdateSummary();
        }
    }

    void StartFindWorking()
    {
        String query = TrimBoth(find_.GetTextUtf8());
        if(query.IsEmpty()) return;
        if(!PrepareSpotifyWorker()) return;
        pending_find_tracks_.Clear();
        SetSpotifyBusy(true, "Searching Spotify for '" + query + "'…");
        if(!spotify_worker_.Run([=] {
            Vector<SpotifyTrack> found;
            String error;
            bool ok = EnsureSpotifyAuthorizedWorker(error);
            if(ok && !spotify_client_.SearchTracks(query, found, 10)) {
                error = spotify_client_.GetLastError(); ok = false;
            }
            {
                GuiLock __;
                pending_find_tracks_ = pick(found);
                pending_spotify_ok_ = ok;
                pending_spotify_error_ = error;
            }
            PostCallback([=] { FinishFindWorking(); });
        })) {
            pending_find_tracks_.Clear();
            SetSpotifyBusy(false, "PlaylistLab could not start the Spotify search worker.");
        }
    }

    void FinishFindWorking()
    {
        bool ok = pending_spotify_ok_;
        String error = pending_spotify_error_;
        SetSpotifyBusy(false);
        if(!ok) { last_notice_ = error.IsEmpty() ? "Spotify search failed." : error; UpdateSummary(); Exclamation(last_notice_); return; }
        if(pending_find_tracks_.IsEmpty()) { last_notice_ = "Spotify returned no track matches."; UpdateSummary(); return; }

        Vector<UiModelItem> rows;
        for(int i = 0; i < pending_find_tracks_.GetCount(); ++i) {
            const SpotifyTrack& track = pending_find_tracks_[i];
            UiModelItem& row = rows.Add();
            row.text = track.title.IsEmpty() ? track.uri : track.title;
            row.description = SpotifyTrackDescription(track);
            row.right_text = DurationText(track.duration_ms);
            row.data = i;
            row.image = GetTrackArtwork(track);
        }
        UiChoiceDialog dialog("Add Spotify Track to Working", rows);
        int selected = dialog.Choose();
        if(selected < 0 || selected >= pending_find_tracks_.GetCount()) return;
        MergeWorkingSource("Spotify Find");
        working_document_.tracks.Add(EntryFromSpotify(pending_find_tracks_[selected]));
        working_document_.dirty = true;
        SaveWorkingState();
        find_.SetTextUtf8(String());
        last_notice_ = "Spotify search result added directly to Working.";
        RefreshWorkingProjection(working_document_.tracks.GetCount() - 1);
        StartTrackArtworkCache();
    }

    void StartResolveWorkingAll()
    {
        SaveInspectorNote();
        if(working_document_.tracks.IsEmpty() || !PrepareSpotifyWorker()) return;
        pending_working_resolution_.Clear();
        PlaylistDocument copy = ClonePlaylistDocument(working_document_);
        pending_working_resolution_.Create();
        Swap(*pending_working_resolution_, copy);
        PlaylistDocument *job = ~pending_working_resolution_;
        inspector_index_ = -1;
        SetSpotifyBusy(true, "Resolving unmatched Working tracks against Spotify…");
        if(!spotify_worker_.Run([=] {
            String error;
            bool ok = EnsureSpotifyAuthorizedWorker(error);
            if(ok && !spotify_client_.ResolveDocument(*job)) { error = spotify_client_.GetLastError(); ok = false; }
            {
                GuiLock __;
                pending_spotify_ok_ = ok;
                pending_spotify_error_ = error;
            }
            PostCallback([=] { FinishResolveWorkingAll(); });
        })) {
            pending_working_resolution_.Clear();
            SetSpotifyBusy(false, "PlaylistLab could not start Working resolution.");
        }
    }

    void FinishResolveWorkingAll()
    {
        bool ok = pending_spotify_ok_;
        String error = pending_spotify_error_;
        SetSpotifyBusy(false);
        if(!ok || !pending_working_resolution_) {
            pending_working_resolution_.Clear();
            last_notice_ = error.IsEmpty() ? "Working resolution failed." : error;
            UpdateSummary(); Exclamation(last_notice_); return;
        }
        Swap(working_document_, *pending_working_resolution_);
        pending_working_resolution_.Clear();
        SaveWorkingState();
        last_notice_ = Format("Resolve complete: %d publishable / %d review / %d missing. Double-click amber rows to review candidates.",
                              working_document_.GetResolvedCount(), working_document_.GetReviewCount(), working_document_.GetMissingCount());
        RefreshWorkingProjection(working_document_.tracks.IsEmpty() ? -1 : 0);
        StartTrackArtworkCache();
    }

    void ReviewWorkingCandidate()
    {
        SaveInspectorNote();
        int index = working_list_.GetCursor();
        if(index < 0 || index >= working_document_.tracks.GetCount()) return;
        TrackEntry& entry = working_document_.tracks[index];
        if(entry.candidates.IsEmpty()) return;
        Vector<UiModelItem> rows;
        for(int i = 0; i < entry.candidates.GetCount(); ++i) {
            const SpotifyTrack& candidate = entry.candidates[i];
            UiModelItem& row = rows.Add();
            row.text = candidate.title.IsEmpty() ? candidate.uri : candidate.title;
            row.description = SpotifyTrackDescription(candidate);
            row.right_text = Format("%d", ScoreTrackCandidate(entry, candidate));
            row.data = i;
            row.image = GetTrackArtwork(candidate);
        }
        UiChoiceDialog dialog("Confirm Spotify Candidate", rows);
        int selected = dialog.Choose(entry.selected_candidate >= 0 ? entry.selected_candidate : 0);
        if(selected < 0) return;
        int score = ScoreTrackCandidate(entry, entry.candidates[selected]);
        entry.SelectCandidate(selected, TRACK_EXACT);
        entry.confidence = score;
        entry.note = "Confirmed by user";
        working_document_.dirty = true;
        SaveWorkingState();
        last_notice_ = "Spotify candidate confirmed locally.";
        RefreshWorkingProjection(index);
        StartTrackArtworkCache();
    }

    void RemoveWorkingSelected()
    {
        SaveInspectorNote();
        Vector<int> rows = SelectedIndices(working_list_);
        if(rows.IsEmpty()) return;
        for(int i = rows.GetCount() - 1; i >= 0; --i)
            if(rows[i] >= 0 && rows[i] < working_document_.tracks.GetCount()) working_document_.tracks.Remove(rows[i]);
        working_document_.dirty = true;
        if(working_document_.tracks.IsEmpty()) working_source_.Clear();
        inspector_index_ = -1;
        SaveWorkingState();
        last_notice_ = Format("Removed %d Working track%s.", rows.GetCount(), rows.GetCount() == 1 ? "" : "s");
        RefreshWorkingProjection();
    }

    void ClearWorking()
    {
        if(!working_document_.tracks.IsEmpty() && !PromptYesNo("Clear the local Working Playlist?\n\nSpotify will not be modified.")) return;
        SaveInspectorNote();
        String name = working_document_.name.IsEmpty() ? String("Working Playlist") : working_document_.name;
        working_document_.tracks.Clear();
        working_document_.source_path.Clear();
        working_document_.name = name;
        working_document_.dirty = true;
        working_source_.Clear();
        inspector_index_ = -1;
        SaveWorkingState();
        RefreshWorkingName();
        last_notice_ = "Working Playlist tracks cleared; its name was kept. Spotify was not modified.";
        RefreshWorkingProjection();
    }

    void ExportWorking()
    {
        CommitWorkingName();
        if(working_document_.tracks.IsEmpty()) return;
        String path = SelectFileSaveAs("CSV files\t*.csv\nAll files\t*.*");
        if(path.IsEmpty()) return;
        if(GetFileExt(path).IsEmpty()) path << ".csv";
        if(!SaveFile(path, ExportPlaylistCsv(working_document_))) { Exclamation("PlaylistLab could not write the Working Playlist CSV."); return; }
        working_document_.source_path = path;
        working_document_.dirty = false;
        SaveWorkingState();
        last_notice_ = "Working Playlist exported. Spotify was not modified.";
        UpdateSummary();
    }

    void ReorderWorking(UiReorderRequest& request)
    {
        if(spotify_busy_) { request.accept = false; return; }
        SaveInspectorNote();
        inspector_index_ = -1;
        int target = request.before;
        if(!working_document_.MoveTrack(request.from, request.before)) { request.accept = false; return; }
        request.handled = true;
        if(target > request.from) target--;
        SaveWorkingState();
        last_notice_ = "Working Playlist reordered locally.";
        RefreshWorkingProjection(target);
    }

    bool GetWorkingPublishUris(Vector<String>& uris, String& error) const
    {
        uris.Clear();
        if(working_document_.tracks.IsEmpty()) { error = "Working Playlist is empty."; return false; }
        for(int i = 0; i < working_document_.tracks.GetCount(); ++i) {
            String uri = working_document_.tracks[i].ResolvedUri();
            if(!IsSpotifyPublishableUri(uri)) {
                error = Format("Working row %d is not publishable. Resolve or review it first.", i + 1);
                uris.Clear(); return false;
            }
            uris.Add(uri);
        }
        return true;
    }

    Vector<UiModelItem> WorkingPreviewRows(const String& right_text) const
    {
        Vector<UiModelItem> rows;
        rows.Reserve(working_document_.tracks.GetCount());
        for(int i = 0; i < working_document_.tracks.GetCount(); ++i) {
            UiModelItem& row = rows.Add();
            row.text = Format("%d. %s", i + 1, TrackDisplayTitle(working_document_.tracks[i]));
            row.description = TrackDisplayDescription(working_document_.tracks[i]);
            row.right_text = right_text;
        }
        return rows;
    }

    void RunDestinationAction(int action)
    {
        CommitWorkingName();
        if(action == DEST_APPEND) ShowAppendPreview();
        else if(action == DEST_REPLACE) ShowReplacePreview();
        else ShowCreatePreview();
    }

    void ShowCreatePreview()
    {
        Vector<String> uris;
        String error;
        if(!GetWorkingPublishUris(uris, error)) { Exclamation(error); return; }
        String name = TrimBoth(working_document_.name);
        if(name.IsEmpty()) name = "PlaylistLab Playlist";
        Vector<UiModelItem> rows = WorkingPreviewRows("NEW");
        PreviewDialog dialog("Create Spotify Playlist", "New Spotify playlist: " + name,
                             Format("%d item%s in exact Working order", uris.GetCount(), uris.GetCount() == 1 ? "" : "s"),
                             "A new private Spotify playlist will be created. Existing Spotify playlists are not changed.",
                             rows, "Create Playlist", true);
        if(dialog.Choose() == IDYES) StartCreateWorkingPlaylist(name, uris);
        else { last_notice_ = "Create preview cancelled. Spotify was not modified."; UpdateSummary(); }
    }

    void StartCreateWorkingPlaylist(const String& name, const Vector<String>& uris)
    {
        if(!PrepareSpotifyWorker()) return;
        pending_create_uris_ = clone(uris);
        pending_create_name_ = name;
        pending_created_playlist_ = SpotifyPlaylistInfo(); pending_create_added_ = 0;
        SetSpotifyBusy(true, "Creating Spotify playlist '" + name + "'…");
        if(!spotify_worker_.Run([=] {
            SpotifyPlaylistInfo created;
            int added = 0;
            String worker_error;
            bool ok = EnsureSpotifyAuthorizedWorker(worker_error);
            if(ok && !spotify_client_.CreatePlaylist(pending_create_name_, false, "Created by PlaylistLab", created)) {
                worker_error = spotify_client_.GetLastError(); ok = false;
            }
            if(ok) {
                String snapshot = created.snapshot_id;
                if(!spotify_client_.AddItems(created.id, pending_create_uris_, &snapshot, &added)) {
                    worker_error = spotify_client_.GetLastError(); ok = false;
                }
                created.snapshot_id = snapshot;
            }
            {
                GuiLock __;
                pending_created_playlist_ = pick(created);
                pending_create_added_ = added;
                pending_spotify_ok_ = ok;
                pending_spotify_error_ = worker_error;
            }
            PostCallback([=] { FinishCreateWorkingPlaylist(); });
        })) {
            pending_create_uris_.Clear(); pending_create_name_.Clear();
            SetSpotifyBusy(false, "PlaylistLab could not start the create-playlist worker.");
        }
    }

    void FinishCreateWorkingPlaylist()
    {
        bool ok = pending_spotify_ok_;
        String error = pending_spotify_error_;
        bool created = !pending_created_playlist_.id.IsEmpty();
        SetSpotifyBusy(false);
        pending_create_uris_.Clear(); pending_create_name_.Clear();
        if(created) { last_playlist_id_ = pending_created_playlist_.id; SaveUiState(); }
        if(!ok) {
            last_notice_ = created
                         ? Format("Spotify created '%s', but population stopped after %d item%s. Inspect the new playlist before retrying: %s",
                                  pending_created_playlist_.name, pending_create_added_, pending_create_added_ == 1 ? "" : "s", ~error)
                         : (error.IsEmpty() ? String("Spotify playlist creation failed.") : error);
            UpdateSummary(); Exclamation(last_notice_);
            if(created) StartLoadPlaylists(false);
            return;
        }
        last_notice_ = Format("Created Spotify playlist '%s' with %d item%s.", pending_created_playlist_.name,
                              pending_create_added_, pending_create_added_ == 1 ? "" : "s");
        UpdateSummary(); StartLoadPlaylists(false);
    }

    void ShowAppendPreview()
    {
        if(!target_loaded_ || !target_editable_ || target_snapshot_id_.IsEmpty()) {
            Exclamation("Select and load an editable Spotify playlist before Append Missing."); return;
        }
        Vector<String> working;
        String error;
        if(!GetWorkingPublishUris(working, error)) { Exclamation(error); return; }
        Vector<String> missing = BuildAppendMissingUris(working, target_uris_);
        if(missing.IsEmpty()) {
            last_notice_ = "Append Missing is a no-op: every Working occurrence is already present in the selected playlist.";
            UpdateSummary(); return;
        }
        Vector<UiModelItem> rows;
        for(const String& uri : missing) {
            UiModelItem& row = rows.Add();
            row.text = uri;
            for(const TrackEntry& entry : working_document_.tracks)
                if(entry.ResolvedUri() == uri) { row.text = TrackDisplayTitle(entry); row.description = TrackDisplayDescription(entry); break; }
            row.right_text = "APPEND";
        }
        PreviewDialog dialog("Append Missing Preview", "Selected Spotify playlist: " + target_playlist_name_,
                             Format("%d missing occurrence%s will be appended", missing.GetCount(), missing.GetCount() == 1 ? "" : "s"),
                             "Only missing Working occurrences are added at the end. Existing items are not reordered or deleted. A fresh snapshot/readback still guards the operation.",
                             rows, "Append Missing", true);
        if(dialog.Choose() == IDYES) StartAppendSelected(working);
        else { last_notice_ = "Append preview cancelled. Spotify was not modified."; UpdateSummary(); }
    }

    void StartAppendSelected(const Vector<String>& working)
    {
        if(!PrepareSpotifyWorker()) return;
        pending_append_working_uris_ = clone(working);
        pending_append_result_.Clear();
        String id = target_playlist_id_, snapshot = target_snapshot_id_;
        SetSpotifyBusy(true, "Appending missing Working tracks to '" + target_playlist_name_ + "'…");
        if(!spotify_worker_.Run([=] {
            SpotifyAppendResult result;
            bool ok = spotify_client_.ExecuteAppendMissing(id, pending_append_working_uris_, snapshot, result);
            String worker_error = ok ? String() : spotify_client_.GetLastError();
            {
                GuiLock __;
                pending_append_result_.Clear();
                pending_append_result_.success = result.success;
                pending_append_result_.stale = result.stale;
                pending_append_result_.partial = result.partial;
                pending_append_result_.observed = result.observed;
                pending_append_result_.added_count = result.added_count;
                pending_append_result_.snapshot_id = result.snapshot_id;
                pending_append_result_.error = result.error;
                pending_append_result_.planned_add_uris = pick(result.planned_add_uris);
                pending_append_result_.observed_tracks = pick(result.observed_tracks);
                pending_append_result_.observed_uris = pick(result.observed_uris);
                pending_spotify_ok_ = ok;
                pending_spotify_error_ = worker_error;
            }
            PostCallback([=] { FinishAppendSelected(); });
        })) {
            pending_append_working_uris_.Clear();
            pending_append_result_.Clear();
            SetSpotifyBusy(false, "PlaylistLab could not start the Append worker.");
        }
    }

    void ApplyObservedTarget(Vector<SpotifyTrack>& tracks, Vector<String>& uris, const String& snapshot)
    {
        target_tracks_ = pick(tracks); target_uris_ = pick(uris); target_snapshot_id_ = snapshot;
        target_loaded_ = true; target_items_accessible_ = true;
        int q = FindPlaylist(target_playlist_id_);
        if(q >= 0) {
            spotify_playlists_[q].items_accessible = true;
            spotify_playlists_[q].item_count = target_tracks_.GetCount();
        }
        RefreshTargetProjection(); RefreshPlaylistProjection(q); StartTrackArtworkCache();
    }

    void FinishAppendSelected()
    {
        bool ok = pending_spotify_ok_;
        String error = pending_spotify_error_;
        SpotifyAppendResult& result = pending_append_result_;
        SetSpotifyBusy(false);
        if(result.observed) ApplyObservedTarget(result.observed_tracks, result.observed_uris, result.snapshot_id);
        else if(result.partial) { target_loaded_ = false; target_tracks_.Clear(); target_uris_.Clear(); target_snapshot_id_.Clear(); RefreshTargetProjection(); }
        pending_append_working_uris_.Clear();
        if(ok && result.success) {
            last_notice_ = result.added_count == 0 ? "Append Missing verified: nothing needed to be added."
                                                   : Format("Append Missing verified: %d item%s appended; no reorder or delete.", result.added_count, result.added_count == 1 ? "" : "s");
            UpdateSummary(); return;
        }
        if(result.stale) last_notice_ = "Append cancelled because the Spotify target changed. Inspect it before retrying.";
        else if(result.partial) last_notice_ = "Append may have partially changed Spotify. Inspect the refreshed target before retrying.";
        else last_notice_ = error.IsEmpty() ? "Spotify Append failed." : error;
        UpdateSummary(); Exclamation(last_notice_);
    }

    void ShowReplacePreview()
    {
        if(!target_loaded_ || !target_editable_ || target_snapshot_id_.IsEmpty()) {
            Exclamation("Select and load an editable Spotify playlist before Replace."); return;
        }
        Vector<String> desired;
        String error;
        if(!GetWorkingPublishUris(desired, error)) { Exclamation(error); return; }
        if(target_uris_ == desired) { last_notice_ = "Replace is a no-op: Spotify already exactly matches Working."; UpdateSummary(); return; }
        int removed = CountOccurrenceDifference(target_uris_, desired);
        int added = CountOccurrenceDifference(desired, target_uris_);
        Vector<UiModelItem> rows = WorkingPreviewRows("FINAL");
        PreviewDialog dialog("Replace Spotify Playlist", "DESTRUCTIVE REPLACE  •  " + target_playlist_name_,
                             Format("Current %d  •  Final %d  •  %d removed  •  %d new", target_uris_.GetCount(), desired.GetCount(), removed, added),
                             "This replaces the entire selected Spotify item sequence with exact Working order. The preview itself is the confirmation; a fresh snapshot preflight and exact final readback are still required.",
                             rows, "Replace Playlist", true);
        if(dialog.Choose() == IDYES) StartReplaceSelected(desired);
        else { last_notice_ = "Replace preview cancelled. Spotify was not modified."; UpdateSummary(); }
    }

    void StartReplaceSelected(const Vector<String>& desired)
    {
        if(!PrepareSpotifyWorker()) return;
        pending_replace_uris_ = clone(desired); pending_replace_result_.Clear();
        String id = target_playlist_id_, snapshot = target_snapshot_id_;
        SetSpotifyBusy(true, "Replacing exact contents of '" + target_playlist_name_ + "'…");
        if(!spotify_worker_.Run([=] {
            SpotifyReplaceResult result;
            bool ok = spotify_client_.ExecuteReplaceItems(id, pending_replace_uris_, snapshot, result);
            String worker_error = ok ? String() : spotify_client_.GetLastError();
            {
                GuiLock __;
                pending_replace_result_.Clear();
                pending_replace_result_.success = result.success;
                pending_replace_result_.stale = result.stale;
                pending_replace_result_.partial = result.partial;
                pending_replace_result_.observed = result.observed;
                pending_replace_result_.overwritten_count = result.overwritten_count;
                pending_replace_result_.written_count = result.written_count;
                pending_replace_result_.appended_count = result.appended_count;
                pending_replace_result_.snapshot_id = result.snapshot_id;
                pending_replace_result_.error = result.error;
                pending_replace_result_.observed_tracks = pick(result.observed_tracks);
                pending_replace_result_.observed_uris = pick(result.observed_uris);
                pending_spotify_ok_ = ok;
                pending_spotify_error_ = worker_error;
            }
            PostCallback([=] { FinishReplaceSelected(); });
        })) {
            pending_replace_uris_.Clear();
            pending_replace_result_.Clear();
            SetSpotifyBusy(false, "PlaylistLab could not start the Replace worker.");
        }
    }

    void FinishReplaceSelected()
    {
        bool ok = pending_spotify_ok_;
        String error = pending_spotify_error_;
        SpotifyReplaceResult& result = pending_replace_result_;
        SetSpotifyBusy(false);
        if(result.observed) ApplyObservedTarget(result.observed_tracks, result.observed_uris, result.snapshot_id);
        else if(result.partial) { target_loaded_ = false; target_tracks_.Clear(); target_uris_.Clear(); target_snapshot_id_.Clear(); RefreshTargetProjection(); }
        pending_replace_uris_.Clear();
        if(ok && result.success) {
            last_notice_ = Format("Replace verified: Spotify now exactly matches %d Working item%s.", result.written_count, result.written_count == 1 ? "" : "s");
            UpdateSummary(); return;
        }
        if(result.stale) last_notice_ = "Replace cancelled because the Spotify target changed.";
        else if(result.partial) last_notice_ = "Replace may have partially changed Spotify. Inspect the refreshed target before retrying.";
        else last_notice_ = error.IsEmpty() ? "Spotify Replace failed." : error;
        UpdateSummary(); Exclamation(last_notice_);
    }

    void PlaySelectedInSpotify()
    {
        int i = working_list_.GetCursor();
        if(i < 0 || i >= working_document_.tracks.GetCount()) return;
        const SpotifyTrack *track = ArtworkTrack(working_document_.tracks[i]);
        if(track && !track->spotify_url.IsEmpty())
            LaunchWebBrowser(track->spotify_url);
    }

private:
    PlaylistDocument working_document_;
    String working_source_;
    String last_notice_ = "Choose a Spotify Client profile, load playlists, then build Working.";

    Vector<SpotifyClientProfile> client_profiles_;
    int selected_profile_ = -1;
    bool rebuilding_profiles_ = false;
    bool updating_working_name_ = false;
    bool updating_inspector_ = false;
    int inspector_index_ = -1;
    int transfer_action_ = TRANSFER_SELECTED;
    int destination_action_ = DEST_CREATE;

    SpotifyAuth spotify_auth_;
    SpotifyClient spotify_client_;
    Thread spotify_worker_;
    bool spotify_busy_ = false;
    bool pending_spotify_ok_ = false;
    String pending_spotify_error_;
    int pending_spotify_status_ = 0;

    Thread artwork_worker_;
    Vector<String> artwork_job_ids_, artwork_job_urls_;
    Vector<Image> artwork_result_images_;
    Thread track_artwork_worker_;
    Vector<String> track_artwork_job_keys_, track_artwork_job_urls_;
    Vector<Image> track_artwork_result_images_;
    VectorMap<String, Image> track_images_;
    Index<String> track_artwork_attempted_;

    Vector<SpotifyPlaylistInfo> spotify_playlists_;
    Vector<Image> playlist_images_;
    bool rebuilding_playlist_model_ = false;
    Vector<SpotifyTrack> target_tracks_;
    Vector<String> target_uris_;
    String target_playlist_id_, target_playlist_name_, target_snapshot_id_, target_spotify_url_;
    bool target_editable_ = false, target_items_accessible_ = false, target_loaded_ = false;
    String last_playlist_id_;

    Vector<SpotifyTrack> pending_find_tracks_;
    One<PlaylistDocument> pending_working_resolution_;
    SpotifyAppendResult pending_append_result_;
    Vector<String> pending_append_working_uris_;
    SpotifyReplaceResult pending_replace_result_;
    Vector<String> pending_replace_uris_;
    Vector<String> pending_create_uris_;
    String pending_create_name_, pending_rename_name_;
    SpotifyPlaylistInfo pending_created_playlist_;
    int pending_create_added_ = 0;

    UiTitleCard header_;
    UiPanel library_panel_, spotify_panel_, working_panel_, inspector_panel_;

    UiLabel library_heading_, library_hint_;
    UiDropdown profile_selector_;
    UiButton profile_add_, profile_edit_, profile_refresh_;
    UiListModel playlist_model_;
    UiItemRenderImage playlist_renderer_;
    UiList playlist_list_;

    UiLabel spotify_heading_, spotify_meta_, spotify_to_working_label_;
    UiSplitButton spotify_transfer_;
    UiButton open_spotify_, rename_spotify_;
    UiListModel target_track_model_;
    PlaylistTransferList target_track_list_;

    UiLabel working_heading_, destination_label_;
    UiLineEdit working_name_, find_;
    UiSplitButton destination_;
    UiButton import_csv_, paste_text_, resolve_unmatched_, export_csv_, remove_working_, clear_working_;
    UiListModel working_model_;
    PlaylistTransferList working_list_;
    UiItemRenderImage track_renderer_;

    UiLabel inspector_heading_, inspector_title_, inspector_artist_, inspector_album_, inspector_time_, inspector_state_, notes_heading_, notice_;
    ImageCtrl inspector_art_;
    UiButton play_spotify_, review_match_;
    UiMultiEdit notes_;
};

} // namespace

GUI_APP_MAIN
{
    UiThemeContext theme;
    theme.preset = APP_THEME;
    theme.mode = APP_MODE;
    UiTheme::Set(theme);

    PlaylistLabWindow window;
    window.OpenMain();
    window.SetForeground();
    window.Run();
}

#endif
