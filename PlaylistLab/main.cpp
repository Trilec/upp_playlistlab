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

Color AppBackground()
{
    return Color(18, 18, 18);
}

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

String OrderModeText(PlaylistOrderMode mode)
{
    return mode == ORDER_REFERENCE_FIRST ? "Working First" : "Keep Existing Slots";
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

String TrackArtworkKey(const SpotifyTrack& track)
{
    return !track.id.IsEmpty() ? track.id : track.uri;
}

const SpotifyTrack *ArtworkTrack(const TrackEntry& entry)
{
    if(const SpotifyTrack *resolved = entry.GetResolved())
        return resolved;
    if(entry.selected_candidate >= 0 && entry.selected_candidate < entry.candidates.GetCount())
        return &entry.candidates[entry.selected_candidate];
    return nullptr;
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

void CopyPlaylistPlan(PlaylistPlan& dst, const PlaylistPlan& src)
{
    dst.original_uris = clone(src.original_uris);
    dst.desired_uris = clone(src.desired_uris);
    dst.moves.Clear();
    dst.moves.Reserve(src.moves.GetCount());
    for(const PlaylistMove& move : src.moves) {
        PlaylistMove& copy = dst.moves.Add();
        copy.from = move.from;
        copy.before = move.before;
        copy.count = move.count;
    }
    dst.missing_reference_uris = clone(src.missing_reference_uris);
    dst.matched_reference_count = src.matched_reference_count;
}

void CopyPublishPreview(PlaylistPublishPreview& dst, const PlaylistPublishPreview& src)
{
    dst.original_target_uris = clone(src.original_target_uris);
    dst.reference_uris = clone(src.reference_uris);
    dst.add_uris = clone(src.add_uris);
    CopyPlaylistPlan(dst.reorder_plan, src.reorder_plan);
    dst.mode = src.mode;
    dst.reference_count = src.reference_count;
    dst.publishable_count = src.publishable_count;
    dst.review_count = src.review_count;
    dst.missing_count = src.missing_count;
    dst.unresolved_count = src.unresolved_count;
    dst.invalid_uri_count = src.invalid_uri_count;
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
        list_style.row_height = DPI(48);
        list_style.show_checks = false;
        list_style.show_icons = false;
        list_style.show_metadata_marker = false;
        list_style.badge_radius = DPI(8);
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

class PreviewDialog : public TopWindow {
public:
    typedef PreviewDialog CLASSNAME;

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
        Add(context_);
        Add(summary_);
        Add(detail_);
        Add(list_);
        Add(action_);
        Add(close_);

        context_.SetText(context);
        summary_.SetText(summary);
        detail_.SetText(detail);
        context_.SetCustomStyle(UiTheme::ResolveLabel(APP_THEME, APP_MODE, UiLabelRole::Title));
        summary_.SetCustomStyle(UiTheme::ResolveLabel(APP_THEME, APP_MODE, UiLabelRole::Body));
        detail_.SetCustomStyle(UiTheme::ResolveLabel(APP_THEME, APP_MODE, UiLabelRole::Caption));

        action_.SetText(action_text);
        action_.Enable(can_action);
        close_.SetText("Close");
        action_.SetCustomStyle(UiTheme::ResolveButton(APP_THEME, APP_MODE, UiButtonRole::Accent));
        close_.SetCustomStyle(UiTheme::ResolveButton(APP_THEME, APP_MODE, UiButtonRole::Subtle));
        action_.WhenAction = [=] { AcceptBreak(IDYES); };
        close_.WhenAction = [=] { RejectBreak(IDCANCEL); };

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
    }

    virtual void Paint(Draw& w) override { w.DrawRect(GetSize(), AppBackground()); }

    virtual void Layout() override
    {
        Rect rc = GetSize();
        int margin = DPI(16), gap = DPI(7), y = margin;
        int w = max(0, rc.GetWidth() - margin * 2);
        context_.SetRect(margin, y, w, DPI(48)); y += DPI(52);
        summary_.SetRect(margin, y, w, DPI(24)); y += DPI(28);
        detail_.SetRect(margin, y, w, DPI(54)); y += DPI(60);

        int button_h = DPI(36), close_w = DPI(96), action_w = DPI(190);
        int button_y = max(y, rc.GetHeight() - margin - button_h);
        list_.SetRect(margin, y, w, max(0, button_y - y - gap));
        close_.SetRect(max(margin, rc.GetWidth() - margin - close_w), button_y, close_w, button_h);
        action_.SetRect(max(margin, rc.GetWidth() - margin - close_w - gap - action_w), button_y, action_w, button_h);
    }

    int Choose() { return Execute(); }

private:
    UiListModel model_;
    UiList list_;
    UiLabel context_, summary_, detail_;
    UiButton action_, close_;
};

class PlaylistLabWindow : public TopWindow {
public:
    typedef PlaylistLabWindow CLASSNAME;

    PlaylistLabWindow()
        : spotify_client_(spotify_auth_)
    {
        Title("PlaylistLab");
        Sizeable().Zoomable();
        SetRect(0, 0, DPI(1360), DPI(900));
        SetMinSize(Size(DPI(1040), DPI(720)));

        LoadUiState();
        LoadClientProfiles();
        LoadWorkingState();
        BuildHeader();
        BuildBody();
        ApplyTheme();
        ConnectEvents();
        RefreshClientProfiles();
        RefreshPlaylistProjection();
        RefreshTargetProjection();
        RefreshImportedProjection();
        RefreshWorkingProjection();
        PostCallback([=] { StartTrackArtworkCache(); });

        if(HasActiveProfile() && spotify_auth_.HasRefreshToken())
            PostCallback([=] { StartLoadPlaylists(false); });
    }

    ~PlaylistLabWindow()
    {
        if(spotify_worker_.IsOpen())
            spotify_worker_.Wait();
        if(artwork_worker_.IsOpen())
            artwork_worker_.Wait();
        if(track_artwork_worker_.IsOpen())
            track_artwork_worker_.Wait();
    }

    virtual void Paint(Draw& w) override { w.DrawRect(GetSize(), AppBackground()); }

    virtual void Close() override
    {
        if(spotify_busy_) {
            last_notice_ = "Finish the current Spotify operation before closing PlaylistLab.";
            UpdateSummary();
            return;
        }
        SaveUiState();
        SaveWorkingState();
        SaveClientProfiles();
        TopWindow::Close();
    }

    virtual void Layout() override
    {
        Rect rc = GetSize();
        const int margin = DPI(12), gap = DPI(10), header_h = DPI(72);
        header_.SetRect(margin, margin, max(0, rc.GetWidth() - margin * 2), header_h);

        int top = margin + header_h + gap;
        int content_h = max(0, rc.GetHeight() - top - margin);
        int content_w = max(0, rc.GetWidth() - margin * 2);
        int library_w = min(DPI(315), max(DPI(250), content_w * 22 / 100));
        int rail_w = min(DPI(290), max(DPI(230), content_w * 20 / 100));
        int center_w = max(DPI(440), content_w - library_w - rail_w - gap * 2);

        library_panel_.SetRect(margin, top, library_w, content_h);
        int center_x = margin + library_w + gap;
        int available = max(0, content_h - gap * 2);
        int spotify_h = available * 34 / 100;
        int imported_h = available * 29 / 100;
        int working_h = max(0, available - spotify_h - imported_h);
        spotify_panel_.SetRect(center_x, top, center_w, spotify_h);
        imported_panel_.SetRect(center_x, top + spotify_h + gap, center_w, imported_h);
        working_panel_.SetRect(center_x, top + spotify_h + gap + imported_h + gap, center_w, working_h);
        rail_panel_.SetRect(center_x + center_w + gap, top, rail_w, content_h);

        LayoutLibrary();
        LayoutSpotify();
        LayoutImported();
        LayoutWorking();
        LayoutRail();
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
            Value mode = map["order_mode"];
            if(!mode.IsVoid() && !IsNull(mode) && (int)mode == (int)ORDER_REFERENCE_FIRST)
                order_mode_value_ = ORDER_REFERENCE_FIRST;
        }
        catch(...) {}
    }

    void SaveUiState() const
    {
        Json json;
        json("last_playlist_id", last_playlist_id_)
            ("order_mode", (int)order_mode_value_);
        SaveFile(UiStatePath(), ~json);
    }

    void LoadWorkingState()
    {
        String error;
        if(!PlaylistLocalState::LoadWorking(working_document_, working_source_, &error))
            last_notice_ = error;
        else if(!working_document_.tracks.IsEmpty())
            last_notice_ = Format("Restored Working Playlist with %d track%s.",
                                  working_document_.tracks.GetCount(),
                                  working_document_.tracks.GetCount() == 1 ? "" : "s");
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

    void BuildHeader()
    {
        Add(header_);
        header_.SetTitle("PlaylistLab")
               .SetSubTitle("Spotify Playlist  •  Imported Playlist  •  Working Playlist")
               .ShowTitleLine(false)
               .SetContentInset(DPI(8));
    }

    void BuildBody()
    {
        Add(library_panel_);
        Add(spotify_panel_);
        Add(imported_panel_);
        Add(working_panel_);
        Add(rail_panel_);

        library_panel_.Add(library_heading_);
        library_panel_.Add(library_hint_);
        library_panel_.Add(profile_selector_);
        library_panel_.Add(profile_add_);
        library_panel_.Add(profile_edit_);
        library_panel_.Add(profile_delete_);
        library_panel_.Add(refresh_spotify_);
        library_panel_.Add(playlist_list_);

        spotify_panel_.Add(spotify_heading_);
        spotify_panel_.Add(spotify_meta_);
        spotify_panel_.Add(spotify_add_selected_);
        spotify_panel_.Add(spotify_add_all_);
        spotify_panel_.Add(open_spotify_);
        spotify_panel_.Add(target_track_list_);

        imported_panel_.Add(imported_heading_);
        imported_panel_.Add(imported_meta_);
        imported_panel_.Add(import_csv_);
        imported_panel_.Add(paste_text_);
        imported_panel_.Add(imported_export_);
        imported_panel_.Add(imported_find_);
        imported_panel_.Add(imported_find_button_);
        imported_panel_.Add(resolve_imported_);
        imported_panel_.Add(review_imported_);
        imported_panel_.Add(imported_add_selected_);
        imported_panel_.Add(imported_add_all_);
        imported_panel_.Add(imported_remove_);
        imported_panel_.Add(imported_clear_);
        imported_panel_.Add(imported_list_);

        working_panel_.Add(working_heading_);
        working_panel_.Add(working_destination_);
        working_panel_.Add(working_meta_);
        working_panel_.Add(working_hint_);
        working_panel_.Add(working_remove_);
        working_panel_.Add(working_clear_);
        working_panel_.Add(working_export_);
        working_panel_.Add(working_list_);

        rail_panel_.Add(placement_heading_);
        rail_panel_.Add(order_mode_);
        rail_panel_.Add(selection_heading_);
        rail_panel_.Add(selection_title_);
        rail_panel_.Add(selection_artist_);
        rail_panel_.Add(selection_state_);
        rail_panel_.Add(resolve_working_);
        rail_panel_.Add(review_working_);
        rail_panel_.Add(publish_heading_);
        rail_panel_.Add(preview_context_);
        rail_panel_.Add(preview_state_);
        rail_panel_.Add(preview_);
        rail_panel_.Add(notice_);

        library_heading_.SetText("SPOTIFY PLAYLISTS");
        library_hint_.SetText("Select a playlist. Ctrl/Shift selects tracks; accessible tracks can be dragged into Working.");
        profile_add_.SetText("Add");
        profile_edit_.SetText("Edit");
        profile_delete_.SetText("Delete");
        refresh_spotify_.SetText("Refresh");

        spotify_heading_.SetText("SPOTIFY PLAYLIST — TARGET / SOURCE");
        spotify_meta_.SetText("Choose a playlist from the library.");
        spotify_add_selected_.SetText("Add Selected");
        spotify_add_all_.SetText("Add All");
        open_spotify_.SetText("Open Spotify");

        imported_heading_.SetText("IMPORTED PLAYLIST");
        imported_meta_.SetText("Stage CSV/text or search Spotify, then review and add tracks to Working.");
        import_csv_.SetText("Import CSV");
        paste_text_.SetText("Paste Text");
        imported_export_.SetText("Export CSV");
        imported_find_.SetPlaceholder("Song title, artist, or both...");
        imported_find_button_.SetText("Find");
        resolve_imported_.SetText("Resolve");
        review_imported_.SetText("Review Match");
        imported_add_selected_.SetText("Add Selected");
        imported_add_all_.SetText("Add All");
        imported_remove_.SetText("Remove");
        imported_clear_.SetText("Clear");

        working_heading_.SetText("WORKING PLAYLIST");
        working_destination_.SetText("Create New Playlist");
        working_destination_.Add("Apply to Selected Playlist", 1);
        working_destination_.Add("Replace Selected Playlist", 2);
        working_destination_.SetPopupMinWidth(DPI(270));
        working_destination_.SetItemDescription(0, "Safely add/reorder Working into the selected editable playlist. No deletion.");
        working_destination_.SetItemDescription(1, "Destructive: make the selected editable playlist exactly match Working after explicit preview and confirmation.");
        working_hint_.SetText("Ctrl/Shift selects. Drag Spotify/Imported rows here to append; drag the right grip to reorder Working.");
        working_remove_.SetText("Remove Selected");
        working_clear_.SetText("Clear");
        working_export_.SetText("Export CSV");

        placement_heading_.SetText("APPLY ORDER");
        order_mode_.Add("Keep Existing Slots", (int)ORDER_REFERENCE_SLOTS);
        order_mode_.Add("Working First", (int)ORDER_REFERENCE_FIRST);
        order_mode_.Select(order_mode_value_ == ORDER_REFERENCE_FIRST ? 1 : 0);
        order_mode_.SetPopupMaxItems(2);

        selection_heading_.SetText("WORKING SELECTION");
        selection_title_.SetText("No Working track selected");
        resolve_working_.SetText("Resolve Selected");
        review_working_.SetText("Review Candidate");
        publish_heading_.SetText("APPLY PREVIEW");
        preview_context_.SetText("Source: Working Playlist\nTarget: none");
        preview_state_.SetText("Select an accessible Spotify target and build the Working Playlist.");
        preview_.SetText("Preview Apply");

        imported_find_.Tip("Search Spotify for a song, artist, or both. Pick one result to add it to Imported Playlist.");
        imported_find_button_.Tip("Search Spotify and choose a result to add to Imported Playlist.");
        resolve_imported_.Tip("Match unresolved Imported rows against Spotify. Strong unique matches auto-link; ambiguous matches remain for Review Match.");
        review_imported_.Tip("Choose the correct Spotify candidate for the selected ambiguous Imported row.");
        resolve_working_.Tip("Search Spotify again for the selected unresolved Working row.");
        review_working_.Tip("Choose the exact Spotify candidate for the selected Working row.");
        order_mode_.Tip("Keep Existing Slots preserves unrelated target positions where possible. Working First places Working occurrences before unrelated target items.");
        preview_.Tip("Preview the safe Apply operation. Apply may add and reorder but never deletes target items.");
        working_destination_.Tip("Primary action creates a new private Spotify playlist. Use the arrow for Apply or explicit Replace of the selected editable playlist.");
        target_track_list_.Tip("Ctrl/Shift selects multiple tracks. Drag selected tracks to Working Playlist or use Add Selected.");
        imported_list_.Tip("Ctrl/Shift selects multiple tracks. Drag selected rows to Working Playlist or use Add Selected.");
        working_list_.Tip("Drop tracks here from Spotify or Imported. Drag the right-side grip to reorder one Working row.");

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

        imported_list_.SetModel(imported_model_)
                      .SetItemRender(track_renderer_)
                      .SetSelectionMode(UILISTSEL_MULTI)
                      .EnableRenameOnDblClick(false)
                      .EnableDragReorder(false)
                      .ShowDragHandle(false);
        imported_list_.EnableTransferSource();

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
    }

    void ApplyTheme()
    {
        header_.SetCustomStyle(UiTheme::ResolveTitleCard(APP_THEME, APP_MODE));
        library_panel_.SetCustomStyle(UiTheme::ResolvePanel(APP_THEME, APP_MODE, UiPanelRole::Subtle));
        spotify_panel_.SetCustomStyle(UiTheme::ResolvePanel(APP_THEME, APP_MODE, UiPanelRole::Surface));
        imported_panel_.SetCustomStyle(UiTheme::ResolvePanel(APP_THEME, APP_MODE, UiPanelRole::Surface));
        working_panel_.SetCustomStyle(UiTheme::ResolvePanel(APP_THEME, APP_MODE, UiPanelRole::Strong));
        rail_panel_.SetCustomStyle(UiTheme::ResolvePanel(APP_THEME, APP_MODE, UiPanelRole::Subtle));

        UiLabel::Style heading = UiTheme::ResolveLabel(APP_THEME, APP_MODE, UiLabelRole::Caption);
        heading.font = SansSerifZ(10).Bold();
        heading.metrics.text_font = heading.font;
        heading.metrics.use_text_font = true;
        for(UiLabel *label : { &library_heading_, &spotify_heading_, &imported_heading_, &working_heading_,
                              &placement_heading_, &selection_heading_, &publish_heading_ })
            label->SetCustomStyle(heading);

        UiLabel::Style body = UiTheme::ResolveLabel(APP_THEME, APP_MODE, UiLabelRole::Body);
        UiLabel::Style caption = UiTheme::ResolveLabel(APP_THEME, APP_MODE, UiLabelRole::Caption);
        for(UiLabel *label : { &library_hint_, &spotify_meta_, &imported_meta_, &working_meta_, &working_hint_,
                              &selection_artist_, &selection_state_, &preview_context_, &preview_state_, &notice_ })
            label->SetCustomStyle(caption);
        selection_title_.SetCustomStyle(body);

        UiButton::Style standard = UiTheme::ResolveButton(APP_THEME, APP_MODE, UiButtonRole::Standard);
        UiButton::Style subtle = UiTheme::ResolveButton(APP_THEME, APP_MODE, UiButtonRole::Subtle);
        UiButton::Style accent = UiTheme::ResolveButton(APP_THEME, APP_MODE, UiButtonRole::Accent);
        for(UiButton *button : { &profile_add_, &profile_edit_, &spotify_add_selected_, &spotify_add_all_,
                                 &import_csv_, &paste_text_, &imported_find_button_, &resolve_imported_,
                                 &imported_add_selected_, &imported_add_all_, &working_remove_, &resolve_working_ })
            button->SetCustomStyle(standard);
        for(UiButton *button : { &profile_delete_, &open_spotify_, &review_imported_, &imported_remove_,
                                 &imported_clear_, &imported_export_, &working_clear_, &working_export_,
                                 &review_working_ })
            button->SetCustomStyle(subtle);
        refresh_spotify_.SetCustomStyle(accent);
        preview_.SetCustomStyle(accent);
        working_destination_.SetCustomStyle(accent);
        profile_selector_.SetCustomStyle(UiTheme::ResolveDropdown(APP_THEME, APP_MODE));
        order_mode_.SetCustomStyle(UiTheme::ResolveDropdown(APP_THEME, APP_MODE));

        UiList::Style playlists = UiTheme::ResolveList(APP_THEME, APP_MODE);
        playlists.row_height = DPI(72);
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
        source.right_text_as_badge = true;
        source.badge_radius = DPI(8);
        target_track_list_.SetCustomStyle(source);

        UiList::Style imported = source;
        imported.show_metadata_marker = true;
        imported_list_.SetCustomStyle(imported);

        UiList::Style working = source;
        working.show_metadata_marker = false;
        working.show_drag_handle = true;
        working.drag_side = UiAlign::RIGHT;
        working.drag_size = DPI(20);
        working.drag_gap = DPI(10);
        working.right_text_as_badge = true;
        working.badge_radius = DPI(8);
        working_list_.SetCustomStyle(working);
    }

    void ConnectEvents()
    {
        profile_selector_.WhenSelect = [=](int) { OnProfileSelected(); };
        profile_add_.WhenAction = [=] { AddClientProfile(); };
        profile_edit_.WhenAction = [=] { EditClientProfile(); };
        profile_delete_.WhenAction = [=] { DeleteClientProfile(); };
        refresh_spotify_.WhenAction = [=] { StartLoadPlaylists(true); };
        playlist_list_.WhenSelection = [=] { OnPlaylistSelection(); };

        spotify_add_selected_.WhenAction = [=] { AddSpotifySelected(); };
        spotify_add_all_.WhenAction = [=] { AddSpotifyAll(); };
        open_spotify_.WhenAction = [=] { OpenTargetInSpotify(); };

        import_csv_.WhenAction = [=] { ImportCsv(); };
        paste_text_.WhenAction = [=] { ImportClipboardText(); };
        imported_find_.WhenAction = [=] { StartFindImported(); };
        imported_find_button_.WhenAction = [=] { StartFindImported(); };
        resolve_imported_.WhenAction = [=] { StartResolveImported(); };
        review_imported_.WhenAction = [=] { ReviewImportedCandidate(); };
        imported_add_selected_.WhenAction = [=] { AddImportedSelected(); };
        imported_add_all_.WhenAction = [=] { AddImportedAll(); };
        imported_remove_.WhenAction = [=] { RemoveImportedSelected(); };
        imported_clear_.WhenAction = [=] { ClearImported(); };
        imported_export_.WhenAction = [=] { ExportImported(); };
        imported_list_.WhenSelection = [=] { UpdateActionState(); };

        working_remove_.WhenAction = [=] { RemoveWorkingSelected(); };
        working_clear_.WhenAction = [=] { ClearWorking(); };
        working_export_.WhenAction = [=] { ExportWorking(); };
        working_destination_.WhenAction = [=] { StartCreateWorkingPlaylist(); };
        working_destination_.WhenSelect = [=](int, const Value& value) {
            int action = IsNull(value) ? 0 : (int)value;
            if(action == 1)
                ShowPreview();
            else if(action == 2)
                ShowReplacePreview();
        };
        working_list_.WhenSelection = [=] { UpdateWorkingSelection(); };
        working_list_.WhenReorderRequest = [=](UiReorderRequest& request) { ReorderWorking(request); };
        working_list_.WhenTransferDrop = [=](const PlaylistTransferList& source, const Vector<int>& rows) {
            if(&source == &target_track_list_)
                AddSpotifyRows(rows, true);
            else if(&source == &imported_list_)
                AddImportedRows(rows, true);
        };

        resolve_working_.WhenAction = [=] { StartResolveWorkingSelected(); };
        review_working_.WhenAction = [=] { ReviewWorkingCandidate(); };
        preview_.WhenAction = [=] { ShowPreview(); };
        order_mode_.WhenSelect = [=](int) {
            order_mode_value_ = order_mode_.GetSelection() == 1 ? ORDER_REFERENCE_FIRST : ORDER_REFERENCE_SLOTS;
            SaveUiState();
            UpdateSummary();
        };
    }

    void LayoutLibrary()
    {
        Rect rc = library_panel_.GetSize();
        int margin = DPI(12), gap = DPI(6), w = max(0, rc.GetWidth() - margin * 2), y = margin;
        library_heading_.SetRect(margin, y, w, DPI(20)); y += DPI(24);
        library_hint_.SetRect(margin, y, w, DPI(38)); y += DPI(44);
        profile_selector_.SetRect(margin, y, w, DPI(32)); y += DPI(38);
        int half = max(0, (w - gap) / 2);
        profile_add_.SetRect(margin, y, half, DPI(30));
        profile_edit_.SetRect(margin + half + gap, y, half, DPI(30)); y += DPI(36);
        profile_delete_.SetRect(margin, y, half, DPI(30));
        refresh_spotify_.SetRect(margin + half + gap, y, half, DPI(30)); y += DPI(40);
        playlist_list_.SetRect(margin, y, w, max(0, rc.GetHeight() - y - margin));
    }

    void LayoutSpotify()
    {
        Rect rc = spotify_panel_.GetSize();
        int margin = DPI(12), w = max(0, rc.GetWidth() - margin * 2);
        int y = margin;
        spotify_heading_.SetRect(margin, y, max(0, w - DPI(300)), DPI(20));
        int bx = max(margin, rc.GetWidth() - margin - DPI(294));
        spotify_add_selected_.SetRect(bx, y, DPI(104), DPI(30));
        spotify_add_all_.SetRect(bx + DPI(110), y, DPI(78), DPI(30));
        open_spotify_.SetRect(bx + DPI(194), y, DPI(100), DPI(30));
        y += DPI(34);
        spotify_meta_.SetRect(margin, y, w, DPI(24)); y += DPI(28);
        target_track_list_.SetRect(margin, y, w, max(0, rc.GetHeight() - y - margin));
    }

    void LayoutImported()
    {
        Rect rc = imported_panel_.GetSize();
        int margin = DPI(12), gap = DPI(5), w = max(0, rc.GetWidth() - margin * 2), y = margin;
        imported_heading_.SetRect(margin, y, max(0, w - DPI(265)), DPI(20));
        int top_x = max(margin, rc.GetWidth() - margin - DPI(260));
        import_csv_.SetRect(top_x, y, DPI(82), DPI(28));
        paste_text_.SetRect(top_x + DPI(87), y, DPI(82), DPI(28));
        imported_export_.SetRect(top_x + DPI(174), y, DPI(86), DPI(28));
        y += DPI(32);

        int find_button_w = DPI(58), resolve_w = DPI(72), review_w = DPI(92);
        int fixed = find_button_w + resolve_w + review_w + gap * 3;
        int edit_w = max(DPI(120), w - fixed);
        imported_find_.SetRect(margin, y, edit_w, DPI(30));
        int x = margin + edit_w + gap;
        imported_find_button_.SetRect(x, y, find_button_w, DPI(30)); x += find_button_w + gap;
        resolve_imported_.SetRect(x, y, resolve_w, DPI(30)); x += resolve_w + gap;
        review_imported_.SetRect(x, y, review_w, DPI(30));
        y += DPI(34);

        imported_meta_.SetRect(margin, y, max(0, w - DPI(310)), DPI(24));
        int actions_x = max(margin, rc.GetWidth() - margin - DPI(305));
        imported_add_selected_.SetRect(actions_x, y, DPI(104), DPI(28));
        imported_add_all_.SetRect(actions_x + DPI(109), y, DPI(72), DPI(28));
        imported_remove_.SetRect(actions_x + DPI(186), y, DPI(62), DPI(28));
        imported_clear_.SetRect(actions_x + DPI(253), y, DPI(52), DPI(28));
        y += DPI(34);
        imported_list_.SetRect(margin, y, w, max(0, rc.GetHeight() - y - margin));
    }

    void LayoutWorking()
    {
        Rect rc = working_panel_.GetSize();
        int margin = DPI(12), w = max(0, rc.GetWidth() - margin * 2), y = margin;
        working_heading_.SetRect(margin, y, max(0, w - DPI(194)), DPI(20));
        working_destination_.SetRect(max(margin, rc.GetWidth() - margin - DPI(188)), y, DPI(188), DPI(30));
        y += DPI(34);

        working_meta_.SetRect(margin, y, max(0, w - DPI(300)), DPI(24));
        int bx = max(margin, rc.GetWidth() - margin - DPI(294));
        working_remove_.SetRect(bx, y, DPI(118), DPI(30));
        working_clear_.SetRect(bx + DPI(124), y, DPI(62), DPI(30));
        working_export_.SetRect(bx + DPI(192), y, DPI(102), DPI(30));
        y += DPI(34);
        working_hint_.SetRect(margin, y, w, DPI(22)); y += DPI(25);
        working_list_.SetRect(margin, y, w, max(0, rc.GetHeight() - y - margin));
    }

    void LayoutRail()
    {
        Rect rc = rail_panel_.GetSize();
        int margin = DPI(14), gap = DPI(7), w = max(0, rc.GetWidth() - margin * 2), y = margin;
        placement_heading_.SetRect(margin, y, w, DPI(20)); y += DPI(24);
        order_mode_.SetRect(margin, y, w, DPI(34)); y += DPI(46);
        selection_heading_.SetRect(margin, y, w, DPI(20)); y += DPI(24);
        selection_title_.SetRect(margin, y, w, DPI(30)); y += DPI(32);
        selection_artist_.SetRect(margin, y, w, DPI(24)); y += DPI(24);
        selection_state_.SetRect(margin, y, w, DPI(28)); y += DPI(34);
        resolve_working_.SetRect(margin, y, w, DPI(34)); y += DPI(34) + gap;
        review_working_.SetRect(margin, y, w, DPI(34)); y += DPI(48);
        publish_heading_.SetRect(margin, y, w, DPI(20)); y += DPI(24);
        preview_context_.SetRect(margin, y, w, DPI(52)); y += DPI(56);
        preview_state_.SetRect(margin, y, w, DPI(72)); y += DPI(78);
        preview_.SetRect(margin, y, w, DPI(36));
        int notice_h = DPI(92);
        notice_.SetRect(margin, max(y + DPI(48), rc.GetHeight() - margin - notice_h), w, notice_h);
    }

    void RefreshClientProfiles()
    {
        rebuilding_profiles_ = true;
        profile_selector_.Clear();
        for(int i = 0; i < client_profiles_.GetCount(); i++)
            profile_selector_.Add(client_profiles_[i].name, i);
        if(HasActiveProfile())
            profile_selector_.Select(selected_profile_);
        else
            profile_selector_.ClearSelection();
        rebuilding_profiles_ = false;
        UpdateSummary();
    }

    bool ProfileClientIdExists(const String& client_id, int except = -1) const
    {
        for(int i = 0; i < client_profiles_.GetCount(); i++)
            if(i != except && client_profiles_[i].client_id == client_id)
                return true;
        return false;
    }

    void AddClientProfile()
    {
        if(spotify_busy_)
            return;
        String name, client_id;
        if(!EditTextNotNull(name, "Add Spotify Client Profile", "Friendly name"))
            return;
        if(!EditTextNotNull(client_id, "Add Spotify Client Profile", "Spotify Client ID"))
            return;
        name = TrimBoth(name);
        client_id = TrimBoth(client_id);
        if(client_id.IsEmpty() || ProfileClientIdExists(client_id)) {
            Exclamation("That Spotify Client ID is empty or already stored.");
            return;
        }
        SpotifyClientProfile& profile = client_profiles_.Add();
        profile.name = name.IsEmpty() ? Format("Spotify Client %d", client_profiles_.GetCount()) : name;
        profile.client_id = client_id;
        selected_profile_ = client_profiles_.GetCount() - 1;
        SaveClientProfiles();
        RefreshClientProfiles();
        ApplySelectedProfile(true);
    }

    void EditClientProfile()
    {
        if(spotify_busy_ || !HasActiveProfile())
            return;
        String name = client_profiles_[selected_profile_].name;
        String client_id = client_profiles_[selected_profile_].client_id;
        if(!EditTextNotNull(name, "Edit Spotify Client Profile", "Friendly name"))
            return;
        if(!EditTextNotNull(client_id, "Edit Spotify Client Profile", "Spotify Client ID"))
            return;
        name = TrimBoth(name);
        client_id = TrimBoth(client_id);
        if(client_id.IsEmpty() || ProfileClientIdExists(client_id, selected_profile_)) {
            Exclamation("That Spotify Client ID is empty or already stored.");
            return;
        }
        client_profiles_[selected_profile_].name = name.IsEmpty() ? "Spotify Client" : name;
        client_profiles_[selected_profile_].client_id = client_id;
        SaveClientProfiles();
        RefreshClientProfiles();
        ApplySelectedProfile(true);
    }

    void DeleteClientProfile()
    {
        if(spotify_busy_ || !HasActiveProfile())
            return;
        String name = client_profiles_[selected_profile_].name;
        if(!PromptYesNo("Delete stored Spotify Client ID profile '" + name + "'?\n\nNo Client Secret is stored by PlaylistLab."))
            return;
        int removed = selected_profile_;
        client_profiles_.Remove(removed);
        selected_profile_ = client_profiles_.IsEmpty() ? -1 : min(removed, client_profiles_.GetCount() - 1);
        SaveClientProfiles();
        RefreshClientProfiles();
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
        if(key.IsEmpty() || track_images_.Find(key) >= 0 ||
           track_artwork_attempted_.Find(key) >= 0 || FindIndex(keys, key) >= 0)
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
        for(const TrackEntry& entry : imported_document_.tracks)
            QueueTrackArtwork(ArtworkTrack(entry), track_artwork_job_keys_, track_artwork_job_urls_);
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
        }))
            last_notice_ = "PlaylistLab could not start the track artwork cache worker.";
    }

    void FinishTrackArtworkCache()
    {
        if(track_artwork_worker_.IsOpen())
            track_artwork_worker_.Wait();
        for(int i = 0; i < track_artwork_job_keys_.GetCount() && i < track_artwork_result_images_.GetCount(); ++i)
            if(!IsNull(track_artwork_result_images_[i])) {
                int q = track_images_.Find(track_artwork_job_keys_[i]);
                if(q < 0)
                    track_images_.Add(track_artwork_job_keys_[i], track_artwork_result_images_[i]);
                else
                    track_images_[q] = track_artwork_result_images_[i];
            }
        track_artwork_result_images_.Clear();
        int imported_cursor = imported_list_.GetCursor();
        int working_cursor = working_list_.GetCursor();
        RefreshTargetProjection();
        RefreshImportedProjection(imported_cursor);
        RefreshWorkingProjection(working_cursor);
        StartTrackArtworkCache();
    }

    void ClearSpotifyView()
    {
        if(artwork_worker_.IsOpen())
            artwork_worker_.Wait();
        artwork_busy_ = false;
        spotify_playlists_.Clear();
        playlist_images_.Clear();
        playlist_model_.Clear();
        ClearTarget();
        RefreshTargetProjection();
    }

    void StartFindImported()
    {
        String query = TrimBoth(imported_find_.GetTextUtf8());
        if(query.IsEmpty()) {
            last_notice_ = "Type a song title, artist, or both before using Find.";
            UpdateSummary();
            return;
        }
        if(!PrepareSpotifyWorker())
            return;

        pending_find_tracks_.Clear();
        SetSpotifyBusy(true, "Searching Spotify for '" + query + "'...");
        if(!spotify_worker_.Run([=] {
            Vector<SpotifyTrack> found;
            String error;
            bool ok = EnsureSpotifyAuthorizedWorker(error);
            if(ok && !spotify_client_.SearchTracks(query, found, 10)) {
                error = spotify_client_.GetLastError();
                ok = false;
            }
            {
                GuiLock __;
                pending_find_tracks_ = pick(found);
                pending_spotify_ok_ = ok;
                pending_spotify_error_ = error;
            }
            PostCallback([=] { FinishFindImported(); });
        }))
            SetSpotifyBusy(false, "PlaylistLab could not start the Spotify search worker.");
    }

    void FinishFindImported()
    {
        bool ok = pending_spotify_ok_;
        String error = pending_spotify_error_;
        SetSpotifyBusy(false);
        if(!ok) {
            last_notice_ = error.IsEmpty() ? "Spotify search failed." : error;
            UpdateSummary();
            Exclamation(last_notice_);
            return;
        }
        if(pending_find_tracks_.IsEmpty()) {
            last_notice_ = "Spotify returned no track matches for that search.";
            UpdateSummary();
            return;
        }

        Vector<UiModelItem> rows;
        rows.Reserve(pending_find_tracks_.GetCount());
        for(int i = 0; i < pending_find_tracks_.GetCount(); ++i) {
            const SpotifyTrack& track = pending_find_tracks_[i];
            UiModelItem& row = rows.Add();
            row.text = track.title.IsEmpty() ? track.uri : track.title;
            row.description = SpotifyTrackDescription(track);
            row.right_text = DurationText(track.duration_ms);
            row.data = i;
            row.image = GetTrackArtwork(track);
        }
        UiChoiceDialog dialog("Add Spotify Track to Imported Playlist", rows);
        int selected = dialog.Choose();
        if(selected < 0 || selected >= pending_find_tracks_.GetCount()) {
            last_notice_ = "Spotify search closed without adding a track.";
            UpdateSummary();
            return;
        }

        imported_document_.tracks.Add(EntryFromSpotify(pending_find_tracks_[selected]));
        imported_document_.dirty = true;
        if(imported_source_.IsEmpty())
            imported_source_ = "Spotify Find";
        else if(imported_source_ != "Spotify Find" && imported_source_ != "Mixed import / search")
            imported_source_ = "Mixed import / search";
        int index = imported_document_.tracks.GetCount() - 1;
        last_notice_ = "Spotify search result added to Imported Playlist. Add it to Working when ready.";
        RefreshImportedProjection(index);
        StartTrackArtworkCache();
    }

    void ImportCsv()
    {
        String path = SelectFileOpen("CSV files\t*.csv\nAll files\t*.*");
        if(path.IsEmpty())
            return;
        if(!FileExists(path)) {
            Exclamation("The selected CSV file could not be opened.");
            return;
        }
        PlaylistImportResult result = ImportPlaylistCsv(LoadFile(path), path);
        int warnings = result.warnings.GetCount();
        imported_document_ = pick(result.document);
        imported_document_.dirty = false;
        imported_source_ = "CSV: " + GetFileName(path);
        last_notice_ = warnings ? Format("Imported Playlist loaded with %d warning%s.", warnings, warnings == 1 ? "" : "s")
                                : "Imported Playlist loaded. Matching will run against Spotify when authorized.";
        RefreshImportedProjection(imported_document_.tracks.IsEmpty() ? -1 : 0);
        if(spotify_auth_.HasAccessToken() || spotify_auth_.HasRefreshToken())
            StartResolveImported();
    }

    void ImportClipboardText()
    {
        String text = ReadClipboardText();
        if(TrimBoth(text).IsEmpty()) {
            Exclamation("The clipboard does not contain a song list.");
            return;
        }
        PlaylistImportResult result = ImportPlaylistText(text, "Clipboard");
        int warnings = result.warnings.GetCount();
        imported_document_ = pick(result.document);
        imported_document_.dirty = !imported_document_.tracks.IsEmpty();
        imported_source_ = "Clipboard";
        last_notice_ = warnings ? Format("Imported Playlist produced %d warning%s.", warnings, warnings == 1 ? "" : "s")
                                : "Clipboard text loaded into Imported Playlist.";
        RefreshImportedProjection(imported_document_.tracks.IsEmpty() ? -1 : 0);
        if(spotify_auth_.HasAccessToken() || spotify_auth_.HasRefreshToken())
            StartResolveImported();
    }

    void ExportImported()
    {
        if(imported_document_.tracks.IsEmpty())
            return;
        String path = SelectFileSaveAs("CSV files\t*.csv\nAll files\t*.*");
        if(path.IsEmpty())
            return;
        if(GetFileExt(path).IsEmpty())
            path << ".csv";
        if(!SaveFile(path, ExportPlaylistCsv(imported_document_))) {
            Exclamation("PlaylistLab could not write the Imported Playlist CSV.");
            return;
        }
        last_notice_ = "Imported Playlist exported. Spotify was not modified.";
        UpdateSummary();
    }

    void ClearImported()
    {
        imported_document_.Clear();
        imported_source_.Clear();
        last_notice_ = "Imported Playlist cleared. Working Playlist was left unchanged.";
        RefreshImportedProjection();
    }

    void RemoveImportedSelected()
    {
        Vector<int> rows = SelectedIndices(imported_list_);
        if(rows.IsEmpty())
            return;
        for(int i = rows.GetCount() - 1; i >= 0; --i)
            if(rows[i] >= 0 && rows[i] < imported_document_.tracks.GetCount())
                imported_document_.tracks.Remove(rows[i]);
        imported_document_.dirty = true;
        last_notice_ = Format("Removed %d track%s from Imported Playlist only.", rows.GetCount(), rows.GetCount() == 1 ? "" : "s");
        RefreshImportedProjection();
    }

    void MergeWorkingSource(const String& source)
    {
        if(working_document_.tracks.IsEmpty())
            working_source_ = source;
        else if(working_source_.IsEmpty())
            working_source_ = source;
        else if(working_source_ != source && working_source_ != "Mixed sources")
            working_source_ = "Mixed sources";
    }

    void AddSpotifyRows(const Vector<int>& rows, bool from_drag)
    {
        if(rows.IsEmpty())
            return;
        MergeWorkingSource("Spotify: " + target_playlist_name_);
        int added = 0;
        for(int row : rows)
            if(row >= 0 && row < target_tracks_.GetCount()) {
                working_document_.tracks.Add(EntryFromSpotify(target_tracks_[row]));
                added++;
            }
        if(added <= 0)
            return;
        working_document_.dirty = true;
        SaveWorkingState();
        last_notice_ = Format(from_drag ? "Dragged %d Spotify track%s into Working Playlist."
                                        : "Added %d selected Spotify track%s to Working Playlist.",
                              added, added == 1 ? "" : "s");
        RefreshWorkingProjection(working_document_.tracks.GetCount() - 1);
        StartTrackArtworkCache();
    }

    void AddSpotifySelected()
    {
        Vector<int> rows = SelectedIndices(target_track_list_);
        if(rows.IsEmpty()) {
            last_notice_ = "Select one or more Spotify tracks first (Ctrl/Shift for multiple).";
            UpdateSummary();
            return;
        }
        AddSpotifyRows(rows, false);
    }

    void AddSpotifyAll()
    {
        if(!target_loaded_ || target_tracks_.IsEmpty())
            return;
        bool was_empty = working_document_.tracks.IsEmpty();
        MergeWorkingSource("Spotify: " + target_playlist_name_);
        for(const SpotifyTrack& track : target_tracks_)
            working_document_.tracks.Add(EntryFromSpotify(track));
        if(was_empty)
            working_document_.name = target_playlist_name_;
        working_document_.dirty = true;
        SaveWorkingState();
        last_notice_ = Format("Added all %d Spotify items to Working Playlist. The Spotify playlist remains the selected target context.", target_tracks_.GetCount());
        RefreshWorkingProjection(working_document_.tracks.IsEmpty() ? -1 : 0);
        StartTrackArtworkCache();
    }

    void AddImportedRows(const Vector<int>& rows, bool from_drag)
    {
        if(rows.IsEmpty())
            return;
        MergeWorkingSource(imported_source_.IsEmpty() ? "Imported Playlist" : "Imported: " + imported_source_);
        int added = 0;
        for(int row : rows)
            if(row >= 0 && row < imported_document_.tracks.GetCount()) {
                working_document_.tracks.Add(CloneTrackEntry(imported_document_.tracks[row]));
                added++;
            }
        if(added <= 0)
            return;
        working_document_.dirty = true;
        SaveWorkingState();
        last_notice_ = Format(from_drag ? "Dragged %d Imported track%s into Working Playlist."
                                        : "Added %d Imported track%s to Working Playlist.",
                              added, added == 1 ? "" : "s");
        RefreshWorkingProjection(working_document_.tracks.GetCount() - 1);
        StartTrackArtworkCache();
    }

    void AddImportedSelected()
    {
        Vector<int> rows = SelectedIndices(imported_list_);
        if(rows.IsEmpty()) {
            last_notice_ = "Select one or more Imported tracks first (Ctrl/Shift for multiple).";
            UpdateSummary();
            return;
        }
        AddImportedRows(rows, false);
    }

    void AddImportedAll()
    {
        if(imported_document_.tracks.IsEmpty())
            return;
        bool was_empty = working_document_.tracks.IsEmpty();
        MergeWorkingSource(imported_source_.IsEmpty() ? "Imported Playlist" : "Imported: " + imported_source_);
        for(const TrackEntry& entry : imported_document_.tracks)
            working_document_.tracks.Add(CloneTrackEntry(entry));
        if(was_empty)
            working_document_.name = imported_document_.name;
        working_document_.dirty = true;
        SaveWorkingState();
        last_notice_ = Format("Added all %d Imported tracks to Working Playlist.", imported_document_.tracks.GetCount());
        RefreshWorkingProjection(working_document_.tracks.IsEmpty() ? -1 : 0);
        StartTrackArtworkCache();
    }

    void RemoveWorkingSelected()
    {
        Vector<int> rows = SelectedIndices(working_list_);
        if(rows.IsEmpty())
            return;
        for(int i = rows.GetCount() - 1; i >= 0; --i)
            if(rows[i] >= 0 && rows[i] < working_document_.tracks.GetCount())
                working_document_.tracks.Remove(rows[i]);
        working_document_.dirty = true;
        if(working_document_.tracks.IsEmpty())
            working_source_.Clear();
        SaveWorkingState();
        last_notice_ = Format("Removed %d track%s from Working Playlist.", rows.GetCount(), rows.GetCount() == 1 ? "" : "s");
        RefreshWorkingProjection();
    }

    void ClearWorking()
    {
        if(!working_document_.tracks.IsEmpty() && !PromptYesNo("Clear the local Working Playlist?\n\nSpotify will not be modified."))
            return;
        working_document_.Clear();
        working_source_.Clear();
        SaveWorkingState();
        last_notice_ = "Working Playlist cleared. Spotify and Imported Playlist were left unchanged.";
        RefreshWorkingProjection();
    }

    void ExportWorking()
    {
        if(working_document_.tracks.IsEmpty())
            return;
        String path = SelectFileSaveAs("CSV files\t*.csv\nAll files\t*.*");
        if(path.IsEmpty())
            return;
        if(GetFileExt(path).IsEmpty())
            path << ".csv";
        if(!SaveFile(path, ExportPlaylistCsv(working_document_))) {
            Exclamation("PlaylistLab could not write the Working Playlist CSV.");
            return;
        }
        working_document_.source_path = path;
        working_document_.name = GetFileTitle(path);
        working_document_.dirty = false;
        SaveWorkingState();
        last_notice_ = "Working Playlist exported. Spotify was not modified.";
        UpdateSummary();
    }

    void ReorderWorking(UiReorderRequest& request)
    {
        if(spotify_busy_) {
            request.accept = false;
            return;
        }
        int target = request.before;
        if(!working_document_.MoveTrack(request.from, request.before)) {
            request.accept = false;
            return;
        }
        request.handled = true;
        if(target > request.from)
            target--;
        SaveWorkingState();
        last_notice_ = "Working Playlist order changed locally. Preview again before publishing.";
        RefreshWorkingProjection(target);
    }

    void RefreshImportedProjection(int selected = -1)
    {
        Vector<UiModelItem> rows;
        rows.Reserve(imported_document_.tracks.GetCount());
        for(int i = 0; i < imported_document_.tracks.GetCount(); i++) {
            const TrackEntry& entry = imported_document_.tracks[i];
            UiModelItem row;
            row.text = Format("%d. %s", i + 1, TrackDisplayTitle(entry));
            row.description = TrackDisplayDescription(entry);
            row.right_text = TrackMatchStateText(entry.state);
            row.data = i;
            row.has_metadata = true;
            row.metadata_color = MatchStateColor(entry.state);
            if(const SpotifyTrack *track = ArtworkTrack(entry))
                row.image = GetTrackArtwork(*track);
            rows.Add(pick(row));
        }
        imported_model_.Clear();
        if(!rows.IsEmpty())
            imported_model_.AddRange(rows);
        if(selected >= 0 && selected < imported_document_.tracks.GetCount())
            imported_list_.SetCursor(selected);
        UpdateSummary();
    }

    void RefreshWorkingProjection(int selected = -1)
    {
        Vector<UiModelItem> rows;
        rows.Reserve(working_document_.tracks.GetCount());
        for(int i = 0; i < working_document_.tracks.GetCount(); i++) {
            const TrackEntry& entry = working_document_.tracks[i];
            UiModelItem row;
            row.text = Format("%d. %s", i + 1, TrackDisplayTitle(entry));
            row.description = TrackDisplayDescription(entry);
            row.right_text = TrackMatchStateText(entry.state);
            row.data = i;
            if(const SpotifyTrack *track = ArtworkTrack(entry))
                row.image = GetTrackArtwork(*track);
            rows.Add(pick(row));
        }
        working_model_.Clear();
        if(!rows.IsEmpty())
            working_model_.AddRange(rows);
        if(selected >= 0 && selected < working_document_.tracks.GetCount())
            working_list_.SetCursor(selected);
        UpdateWorkingSelection();
        UpdateSummary();
    }

    void RefreshPlaylistProjection(int selected = -1)
    {
        Vector<UiModelItem> rows;
        rows.Reserve(spotify_playlists_.GetCount());
        for(int i = 0; i < spotify_playlists_.GetCount(); i++) {
            const SpotifyPlaylistInfo& playlist = spotify_playlists_[i];
            UiModelItem row;
            row.text = playlist.name.IsEmpty() ? String("Untitled Spotify playlist") : playlist.name;
            String owner = playlist.owner_name.IsEmpty() ? playlist.owner_id : playlist.owner_name;
            row.description = owner.IsEmpty() ? String("Spotify playlist") : "by " + owner;
            String access = playlist.items_accessible ? (playlist.editable ? "EDIT" : "SOURCE") : "META";
            row.right_text = Format("%d  %s", playlist.item_count, access);
            row.data = i;
            if(i < playlist_images_.GetCount())
                row.image = playlist_images_[i];
            rows.Add(pick(row));
        }
        rebuilding_playlist_model_ = true;
        playlist_model_.Clear();
        if(!rows.IsEmpty())
            playlist_model_.AddRange(rows);
        if(selected >= 0 && selected < spotify_playlists_.GetCount())
            playlist_list_.SetCursor(selected);
        rebuilding_playlist_model_ = false;
        UpdateSummary();
    }

    void RefreshTargetProjection()
    {
        Vector<UiModelItem> rows;
        rows.Reserve(target_tracks_.GetCount());
        for(int i = 0; i < target_tracks_.GetCount(); i++) {
            const SpotifyTrack& track = target_tracks_[i];
            UiModelItem row;
            row.text = Format("%d. %s", i + 1, track.title.IsEmpty() ? String("Untitled Spotify item") : track.title);
            row.description = SpotifyTrackDescription(track);
            row.right_text = track.placeholder ? String("UNAVAILABLE") : DurationText(track.duration_ms);
            row.data = i;
            row.image = GetTrackArtwork(track);
            rows.Add(pick(row));
        }
        target_track_model_.Clear();
        if(!rows.IsEmpty())
            target_track_model_.AddRange(rows);
        UpdateSummary();
    }

    void UpdateWorkingSelection()
    {
        int index = working_list_.GetCursor();
        if(index < 0 || index >= working_document_.tracks.GetCount()) {
            selection_title_.SetText("No Working track selected");
            selection_artist_.SetText(Null);
            selection_state_.SetText(Null);
            UpdateActionState();
            return;
        }
        const TrackEntry& entry = working_document_.tracks[index];
        selection_title_.SetText(TrackDisplayTitle(entry));
        selection_artist_.SetText(entry.ResolvedArtist().IsEmpty() ? "Artist: -" : "Artist: " + entry.ResolvedArtist());
        String state = "State: " + TrackMatchStateText(entry.state);
        if(!entry.ResolvedUri().IsEmpty())
            state << "  •  Spotify linked";
        selection_state_.SetText(state);
        UpdateActionState();
    }

    bool GetWorkingPublishUris(Vector<String>& uris, String& error) const
    {
        uris.Clear();
        if(working_document_.tracks.IsEmpty()) {
            error = "Working Playlist is empty.";
            return false;
        }
        uris.Reserve(working_document_.tracks.GetCount());
        for(int i = 0; i < working_document_.tracks.GetCount(); ++i) {
            String uri = working_document_.tracks[i].ResolvedUri();
            if(!IsSpotifyPublishableUri(uri)) {
                error = Format("Working Playlist row %d is not an exact/publishable Spotify item. Resolve or review it first.", i + 1);
                uris.Clear();
                return false;
            }
            uris.Add(uri);
        }
        return true;
    }

    void UpdateSummary()
    {
        int imported_total = imported_document_.tracks.GetCount();
        int working_total = working_document_.tracks.GetCount();
        int working_resolved = working_document_.GetResolvedCount();
        int working_review = working_document_.GetReviewCount();
        int working_missing = working_document_.GetMissingCount();

        String subtitle = Format("%d Spotify playlists  •  %d imported  •  %d working",
                                 spotify_playlists_.GetCount(), imported_total, working_total);
        if(target_playlist_id_.GetCount())
            subtitle << "  •  target: " << target_playlist_name_;
        if(spotify_busy_)
            subtitle << "  •  Spotify working";
        header_.SetSubTitle(subtitle);

        if(target_playlist_id_.IsEmpty())
            spotify_meta_.SetText("No target selected. Choose a Spotify playlist from the library.");
        else if(!target_items_accessible_)
            spotify_meta_.SetText(target_playlist_name_ + "  •  metadata only  •  Spotify refused item access for this account/app.");
        else if(!target_loaded_)
            spotify_meta_.SetText("Loading " + target_playlist_name_ + "...");
        else
            spotify_meta_.SetText(Format("%s  •  %d items  •  %s",
                                         target_playlist_name_, target_tracks_.GetCount(),
                                         target_editable_ ? "editable target" : "readable source"));

        int imported_review = imported_document_.GetReviewCount();
        int imported_missing = imported_document_.GetMissingCount();
        imported_meta_.SetText(Format("%d tracks  •  %d publishable  •  %d review  •  %d missing  •  %s",
                                      imported_total, imported_document_.GetResolvedCount(),
                                      imported_review, imported_missing,
                                      imported_source_.IsEmpty() ? "not loaded" : ~imported_source_));

        working_meta_.SetText(Format("%d tracks  •  %d publishable  •  source: %s",
                                     working_total, working_resolved,
                                     working_source_.IsEmpty() ? "local / mixed not set" : ~working_source_));

        preview_context_.SetText("Source/reference: " + (working_source_.IsEmpty() ? String("Working Playlist") : working_source_) +
                                 "\nTarget: " + (target_playlist_name_.IsEmpty() ? String("none") : target_playlist_name_));

        if(target_loaded_ && working_total > 0) {
            PlaylistPublishPreview p = BuildPlaylistPublishPreview(working_document_, target_uris_, order_mode_value_);
            String prefix = target_editable_ ? OrderModeText(order_mode_value_) : String("Read-only target");
            preview_state_.SetText(Format("%s\n%d add / %d move / %d blocked",
                                          prefix, p.add_uris.GetCount(), p.reorder_plan.moves.GetCount(), p.GetBlockingCount()));
        }
        else if(!target_items_accessible_ && !target_playlist_id_.IsEmpty())
            preview_state_.SetText("Spotify refused this playlist's item list. It remains visible as metadata only.");
        else if(!target_loaded_)
            preview_state_.SetText("Choose an accessible Spotify target.");
        else
            preview_state_.SetText("Add tracks to Working Playlist before previewing.");

        if(working_review || working_missing)
            preview_state_.SetText(preview_state_.GetText() + Format("\nWorking needs %d review / %d missing.", working_review, working_missing));

        notice_.SetText(last_notice_.IsEmpty()
                        ? "No Spotify write occurs until the exact operation is explicitly previewed and confirmed."
                        : last_notice_);
        UpdateActionState();
    }

    void UpdateActionState()
    {
        bool idle = !spotify_busy_;
        bool has_profile = HasActiveProfile();
        bool spotify_source = idle && target_loaded_ && !target_tracks_.IsEmpty();
        int working_index = working_list_.GetCursor();
        const TrackEntry *working_entry = working_index >= 0 && working_index < working_document_.tracks.GetCount()
                                        ? &working_document_.tracks[working_index] : nullptr;
        int imported_index = imported_list_.GetCursor();
        const TrackEntry *imported_entry = imported_index >= 0 && imported_index < imported_document_.tracks.GetCount()
                                         ? &imported_document_.tracks[imported_index] : nullptr;

        profile_selector_.Enable(idle && !client_profiles_.IsEmpty());
        profile_add_.Enable(idle);
        profile_edit_.Enable(idle && has_profile);
        profile_delete_.Enable(idle && has_profile);
        refresh_spotify_.Enable(idle && has_profile);
        playlist_list_.Enable(idle && !spotify_playlists_.IsEmpty());

        target_track_list_.Enable(spotify_source);
        spotify_add_selected_.Enable(spotify_source && target_track_list_.GetSelectionCount() > 0);
        spotify_add_all_.Enable(spotify_source);
        open_spotify_.Enable(idle && !target_spotify_url_.IsEmpty());

        import_csv_.Enable(idle);
        paste_text_.Enable(idle);
        imported_find_.Enable(idle && has_profile);
        imported_find_button_.Enable(idle && has_profile);
        resolve_imported_.Enable(idle && has_profile && !imported_document_.tracks.IsEmpty());
        review_imported_.Enable(idle && imported_entry && !imported_entry->candidates.IsEmpty());
        imported_add_selected_.Enable(idle && imported_list_.GetSelectionCount() > 0);
        imported_add_all_.Enable(idle && !imported_document_.tracks.IsEmpty());
        imported_remove_.Enable(idle && imported_list_.GetSelectionCount() > 0);
        imported_clear_.Enable(idle && !imported_document_.tracks.IsEmpty());
        imported_export_.Enable(idle && !imported_document_.tracks.IsEmpty());
        imported_list_.Enable(idle && !imported_document_.tracks.IsEmpty());

        working_remove_.Enable(idle && working_list_.GetSelectionCount() > 0);
        working_clear_.Enable(idle && !working_document_.tracks.IsEmpty());
        working_export_.Enable(idle && !working_document_.tracks.IsEmpty());
        // Empty Working is still an active drop target for Spotify/Imported rows.
        working_list_.Enable(idle);
        working_destination_.Enable(idle && has_profile && !working_document_.tracks.IsEmpty());
        if(working_destination_.GetCount() >= 2) {
            bool editable_target = idle && target_loaded_ && target_editable_ && !target_snapshot_id_.IsEmpty();
            working_destination_.SetItemEnabled(0, editable_target);
            working_destination_.SetItemEnabled(1, editable_target);
        }
        order_mode_.Enable(idle);
        resolve_working_.Enable(idle && working_entry &&
                                (working_entry->state == TRACK_UNRESOLVED || working_entry->state == TRACK_REVIEW || working_entry->state == TRACK_MISSING));
        review_working_.Enable(idle && working_entry && !working_entry->candidates.IsEmpty());
        preview_.Enable(idle && target_loaded_ && !working_document_.tracks.IsEmpty());
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
        if(!notice.IsEmpty())
            last_notice_ = notice;
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

        SetSpotifyBusy(true, "Connecting to Spotify and loading playlist metadata...");
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

        // Readability and editability are separate. /me/playlists can include
        // followed playlists whose items Spotify will later refuse. Optimistically
        // try the real item endpoint when selected, then mark META only on 403/404.
        for(SpotifyPlaylistInfo& playlist : spotify_playlists_)
            playlist.items_accessible = true;

        playlist_images_.SetCount(spotify_playlists_.GetCount());
        for(int i = 0; i < spotify_playlists_.GetCount(); i++)
            playlist_images_[i] = SpotifyImageCache::Load("playlist-" + spotify_playlists_[i].id);

        int selected = FindPlaylist(last_playlist_id_);
        if(selected < 0 && !spotify_playlists_.IsEmpty())
            selected = 0;
        RefreshPlaylistProjection(selected);
        StartArtworkCache();

        if(spotify_playlists_.IsEmpty()) {
            ClearTarget();
            RefreshTargetProjection();
            last_notice_ = "Spotify returned no playlists for this account.";
            UpdateSummary();
            return;
        }
        last_notice_ = Format("Loaded %d Spotify playlist%s. Item readability is tested when a playlist is selected; editability is tracked separately.",
                              spotify_playlists_.GetCount(), spotify_playlists_.GetCount() == 1 ? "" : "s");
        UpdateSummary();
        if(selected >= 0)
            StartLoadTarget(selected);
    }

    int FindPlaylist(const String& id) const
    {
        for(int i = 0; i < spotify_playlists_.GetCount(); i++)
            if(spotify_playlists_[i].id == id)
                return i;
        return -1;
    }

    void StartArtworkCache()
    {
        if(artwork_worker_.IsOpen())
            artwork_worker_.Wait();
        artwork_job_ids_.Clear();
        artwork_job_urls_.Clear();
        const int max_fetch = 16;
        for(int i = 0; i < spotify_playlists_.GetCount() && artwork_job_ids_.GetCount() < max_fetch; i++) {
            if(i < playlist_images_.GetCount() && !IsNull(playlist_images_[i]))
                continue;
            if(spotify_playlists_[i].image_url.IsEmpty())
                continue;
            artwork_job_ids_.Add(spotify_playlists_[i].id);
            artwork_job_urls_.Add(spotify_playlists_[i].image_url);
        }
        if(artwork_job_ids_.IsEmpty())
            return;

        artwork_busy_ = true;
        if(!artwork_worker_.Run([=] {
            Vector<Image> images;
            images.SetCount(artwork_job_ids_.GetCount());
            for(int i = 0; i < artwork_job_ids_.GetCount(); i++) {
                String ignored;
                images[i] = SpotifyImageCache::LoadOrFetch("playlist-" + artwork_job_ids_[i], artwork_job_urls_[i], &ignored);
            }
            {
                GuiLock __;
                artwork_result_images_ = pick(images);
            }
            PostCallback([=] { FinishArtworkCache(); });
        }))
            artwork_busy_ = false;
    }

    void FinishArtworkCache()
    {
        artwork_busy_ = false;
        for(int i = 0; i < artwork_job_ids_.GetCount() && i < artwork_result_images_.GetCount(); i++) {
            if(IsNull(artwork_result_images_[i]))
                continue;
            int q = FindPlaylist(artwork_job_ids_[i]);
            if(q >= 0 && q < playlist_images_.GetCount())
                playlist_images_[q] = artwork_result_images_[i];
        }
        artwork_result_images_.Clear();
        int selected = playlist_list_.GetCursor();
        RefreshPlaylistProjection(selected);
        if(artwork_worker_.IsOpen())
            artwork_worker_.Wait();
        StartArtworkCache();
    }

    void OnPlaylistSelection()
    {
        if(rebuilding_playlist_model_ || spotify_busy_)
            return;
        int index = playlist_list_.GetCursor();
        if(index < 0 || index >= spotify_playlists_.GetCount())
            return;
        if(target_playlist_id_ == spotify_playlists_[index].id && (target_loaded_ || !target_items_accessible_))
            return;
        StartLoadTarget(index);
    }

    void ClearTarget()
    {
        target_playlist_id_.Clear();
        target_playlist_name_.Clear();
        target_snapshot_id_.Clear();
        target_spotify_url_.Clear();
        target_editable_ = false;
        target_items_accessible_ = false;
        target_loaded_ = false;
        target_tracks_.Clear();
        target_uris_.Clear();
    }

    void StartLoadTarget(int playlist_index)
    {
        if(playlist_index < 0 || playlist_index >= spotify_playlists_.GetCount())
            return;
        const SpotifyPlaylistInfo& playlist = spotify_playlists_[playlist_index];
        target_playlist_id_ = playlist.id;
        target_playlist_name_ = playlist.name;
        target_spotify_url_ = playlist.spotify_url;
        target_editable_ = playlist.editable;
        target_items_accessible_ = playlist.items_accessible;
        target_snapshot_id_.Clear();
        target_loaded_ = false;
        target_tracks_.Clear();
        target_uris_.Clear();
        last_playlist_id_ = target_playlist_id_;
        SaveUiState();
        RefreshTargetProjection();

        if(!target_items_accessible_) {
            last_notice_ = "Spotify previously refused item access for this playlist. Refresh the library to retry its current access state.";
            UpdateSummary();
            return;
        }
        if(!PrepareSpotifyWorker())
            return;

        String playlist_id = target_playlist_id_;
        SetSpotifyBusy(true, "Loading '" + target_playlist_name_ + "' from Spotify...");
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
                if(q >= 0)
                    spotify_playlists_[q].items_accessible = false;
                last_notice_ = "Playlist unavailable or not accessible through Spotify's playlist-items API for the current account. Its metadata remains visible.";
            }
            else
                last_notice_ = error.IsEmpty() ? "Spotify playlist loading failed." : error;
            RefreshTargetProjection();
            RefreshPlaylistProjection(q);
            Exclamation(last_notice_);
            return;
        }

        target_uris_.Clear();
        target_uris_.Reserve(target_tracks_.GetCount());
        for(const SpotifyTrack& track : target_tracks_)
            target_uris_.Add(track.uri);
        target_items_accessible_ = true;
        target_loaded_ = true;
        if(q >= 0) {
            spotify_playlists_[q].items_accessible = true;
            spotify_playlists_[q].item_count = target_tracks_.GetCount();
        }
        last_notice_ = Format("Loaded '%s' with %d item%s. Select or drag tracks into Working Playlist.",
                              target_playlist_name_, target_tracks_.GetCount(), target_tracks_.GetCount() == 1 ? "" : "s");
        RefreshTargetProjection();
        RefreshPlaylistProjection(q);
        StartTrackArtworkCache();
    }

    void OpenTargetInSpotify()
    {
        if(!target_spotify_url_.IsEmpty())
            LaunchWebBrowser(target_spotify_url_);
    }

    void StartResolveImported()
    {
        if(imported_document_.tracks.IsEmpty() || !PrepareSpotifyWorker())
            return;
        pending_import_resolution_.Clear();
        PlaylistDocument copy = ClonePlaylistDocument(imported_document_);
        pending_import_resolution_.Create();
        Swap(*pending_import_resolution_, copy);
        PlaylistDocument *job = ~pending_import_resolution_;

        SetSpotifyBusy(true, "Matching Imported Playlist against Spotify...");
        if(!spotify_worker_.Run([=] {
            String error;
            bool ok = EnsureSpotifyAuthorizedWorker(error);
            if(ok && !spotify_client_.ResolveDocument(*job)) {
                error = spotify_client_.GetLastError();
                ok = false;
            }
            {
                GuiLock __;
                pending_spotify_ok_ = ok;
                pending_spotify_error_ = error;
            }
            PostCallback([=] { FinishResolveImported(); });
        })) {
            pending_import_resolution_.Clear();
            SetSpotifyBusy(false, "PlaylistLab could not start Imported Playlist matching.");
        }
    }

    void FinishResolveImported()
    {
        bool ok = pending_spotify_ok_;
        String error = pending_spotify_error_;
        SetSpotifyBusy(false);
        if(!ok || !pending_import_resolution_) {
            pending_import_resolution_.Clear();
            last_notice_ = error.IsEmpty() ? "Imported Playlist matching failed." : error;
            UpdateSummary();
            Exclamation(last_notice_);
            return;
        }
        Swap(imported_document_, *pending_import_resolution_);
        pending_import_resolution_.Clear();
        int review = imported_document_.GetReviewCount();
        int missing = imported_document_.GetMissingCount();
        last_notice_ = Format("Imported matching complete: %d publishable, %d review, %d missing. Review ambiguous rows before adding them to Working.",
                              imported_document_.GetResolvedCount(), review, missing);
        RefreshImportedProjection(imported_document_.tracks.IsEmpty() ? -1 : 0);
        StartTrackArtworkCache();
    }

    void ReviewCandidate(PlaylistDocument& document, int index, bool working)
    {
        if(index < 0 || index >= document.tracks.GetCount())
            return;
        TrackEntry& entry = document.tracks[index];
        if(entry.candidates.IsEmpty())
            return;

        Vector<UiModelItem> rows;
        for(int i = 0; i < entry.candidates.GetCount(); i++) {
            const SpotifyTrack& candidate = entry.candidates[i];
            UiModelItem& row = rows.Add();
            row.text = candidate.title.IsEmpty() ? candidate.uri : candidate.title;
            row.description = candidate.artist;
            if(!candidate.album.IsEmpty()) {
                if(!row.description.IsEmpty()) row.description << "  •  ";
                row.description << candidate.album;
            }
            row.right_text = Format("%d", ScoreTrackCandidate(entry, candidate));
            row.data = i;
            row.image = GetTrackArtwork(candidate);
        }
        UiChoiceDialog dialog("Confirm Spotify Candidate", rows);
        int selected = dialog.Choose(entry.selected_candidate >= 0 ? entry.selected_candidate : 0);
        if(selected < 0)
            return;
        int score = ScoreTrackCandidate(entry, entry.candidates[selected]);
        entry.SelectCandidate(selected, TRACK_EXACT);
        entry.confidence = score;
        entry.note = "Confirmed by user";
        document.dirty = true;
        if(working) {
            SaveWorkingState();
            last_notice_ = "Working candidate confirmed. Preview is still required before any Spotify write.";
            RefreshWorkingProjection(index);
        }
        else {
            last_notice_ = "Imported candidate confirmed. Add it to Working Playlist when ready.";
            RefreshImportedProjection(index);
        }
        StartTrackArtworkCache();
    }

    void ReviewImportedCandidate()
    {
        ReviewCandidate(imported_document_, imported_list_.GetCursor(), false);
    }

    void ReviewWorkingCandidate()
    {
        ReviewCandidate(working_document_, working_list_.GetCursor(), true);
    }

    void StartResolveWorkingSelected()
    {
        int index = working_list_.GetCursor();
        if(index < 0 || index >= working_document_.tracks.GetCount() || !PrepareSpotifyWorker())
            return;
        pending_resolution_index_ = index;
        pending_resolution_.Clear();
        pending_resolution_.Create() = CloneTrackEntry(working_document_.tracks[index]);
        TrackEntry *job = ~pending_resolution_;
        SetSpotifyBusy(true, "Resolving Working track against Spotify...");
        if(!spotify_worker_.Run([=] {
            String error;
            bool ok = EnsureSpotifyAuthorizedWorker(error);
            if(ok && !spotify_client_.ResolveTrack(*job)) {
                error = spotify_client_.GetLastError();
                ok = false;
            }
            {
                GuiLock __;
                pending_spotify_ok_ = ok;
                pending_spotify_error_ = error;
            }
            PostCallback([=] { FinishResolveWorkingSelected(); });
        })) {
            pending_resolution_.Clear();
            SetSpotifyBusy(false, "PlaylistLab could not start Working track resolution.");
        }
    }

    void FinishResolveWorkingSelected()
    {
        bool ok = pending_spotify_ok_;
        String error = pending_spotify_error_;
        int index = pending_resolution_index_;
        SetSpotifyBusy(false);
        if(!ok || !pending_resolution_) {
            pending_resolution_.Clear();
            last_notice_ = error.IsEmpty() ? "Working track resolution failed." : error;
            UpdateSummary();
            Exclamation(last_notice_);
            return;
        }
        if(index < 0 || index >= working_document_.tracks.GetCount()) {
            pending_resolution_.Clear();
            last_notice_ = "Working Playlist changed before resolution completed.";
            UpdateSummary();
            return;
        }
        Swap(working_document_.tracks[index], *pending_resolution_);
        pending_resolution_.Clear();
        working_document_.dirty = true;
        SaveWorkingState();
        last_notice_ = working_document_.tracks[index].state == TRACK_REVIEW
                     ? "Spotify candidates loaded. Review the preferred candidate before publishing."
                     : "Working track resolution updated locally.";
        RefreshWorkingProjection(index);
        StartTrackArtworkCache();
    }

    String DisplayTitleForUri(const String& uri) const
    {
        for(const TrackEntry& entry : working_document_.tracks)
            if(entry.ResolvedUri() == uri)
                return TrackDisplayTitle(entry);
        for(const SpotifyTrack& track : target_tracks_)
            if(track.uri == uri && !track.title.IsEmpty())
                return track.title;
        return uri;
    }

    String DisplayDescriptionForUri(const String& uri) const
    {
        for(const TrackEntry& entry : working_document_.tracks)
            if(entry.ResolvedUri() == uri) {
                String text = TrackDisplayDescription(entry);
                return text.IsEmpty() ? uri : text + "  •  " + uri;
            }
        for(const SpotifyTrack& track : target_tracks_)
            if(track.uri == uri) {
                String text = SpotifyTrackDescription(track);
                return text.IsEmpty() ? uri : text + "  •  " + uri;
            }
        return uri;
    }

    void StartCreateWorkingPlaylist()
    {
        Vector<String> uris;
        String error;
        if(!GetWorkingPublishUris(uris, error)) {
            Exclamation(error);
            return;
        }
        String name = working_document_.name;
        if(name.IsEmpty())
            name = target_playlist_name_.IsEmpty() ? String("PlaylistLab Playlist") : target_playlist_name_ + " - PlaylistLab";
        if(!EditTextNotNull(name, "Create Spotify Playlist", "Playlist name"))
            return;
        name = TrimBoth(name);
        if(name.IsEmpty())
            return;
        if(!PromptYesNo(Format("Create a new PRIVATE Spotify playlist '%s' with %d item%s?\n\nThis creates a new playlist; it does not change the currently selected target.",
                               name, uris.GetCount(), uris.GetCount() == 1 ? "" : "s")))
            return;
        if(!PrepareSpotifyWorker())
            return;

        pending_create_uris_ = clone(uris);
        pending_created_playlist_ = SpotifyPlaylistInfo();
        pending_create_added_ = 0;
        SetSpotifyBusy(true, "Creating Spotify playlist '" + name + "'...");
        if(!spotify_worker_.Run([=] {
            SpotifyPlaylistInfo created;
            int added = 0;
            String worker_error;
            bool ok = EnsureSpotifyAuthorizedWorker(worker_error);
            if(ok && !spotify_client_.CreatePlaylist(name, false, "Created by PlaylistLab", created)) {
                worker_error = spotify_client_.GetLastError();
                ok = false;
            }
            if(ok) {
                String snapshot = created.snapshot_id;
                if(!spotify_client_.AddItems(created.id, pending_create_uris_, &snapshot, &added)) {
                    worker_error = spotify_client_.GetLastError();
                    ok = false;
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
            pending_create_uris_.Clear();
            SetSpotifyBusy(false, "PlaylistLab could not start the create-playlist worker.");
        }
    }

    void FinishCreateWorkingPlaylist()
    {
        bool ok = pending_spotify_ok_;
        String error = pending_spotify_error_;
        bool created = !pending_created_playlist_.id.IsEmpty();
        SetSpotifyBusy(false);
        pending_create_uris_.Clear();
        if(created) {
            last_playlist_id_ = pending_created_playlist_.id;
            SaveUiState();
        }
        if(!ok) {
            last_notice_ = created
                         ? Format("Spotify created '%s', but population stopped after %d item%s. Inspect that new playlist before retrying: %s",
                                  pending_created_playlist_.name, pending_create_added_, pending_create_added_ == 1 ? "" : "s", ~error)
                         : (error.IsEmpty() ? String("Spotify playlist creation failed.") : error);
            UpdateSummary();
            Exclamation(last_notice_);
            if(created)
                StartLoadPlaylists(false);
            return;
        }
        last_notice_ = Format("Created Spotify playlist '%s' with %d item%s.",
                              pending_created_playlist_.name, pending_create_added_, pending_create_added_ == 1 ? "" : "s");
        UpdateSummary();
        StartLoadPlaylists(false);
    }

    void StartReplaceSelected(const Vector<String>& desired)
    {
        if(!target_editable_ || !target_loaded_ || target_snapshot_id_.IsEmpty()) {
            Exclamation("Choose and reload an editable Spotify target before replacing it.");
            return;
        }
        if(!PrepareSpotifyWorker())
            return;
        pending_replace_uris_ = clone(desired);
        pending_replace_result_.Clear();
        String playlist_id = target_playlist_id_;
        String expected_snapshot = target_snapshot_id_;
        SetSpotifyBusy(true, "Replacing exact contents of '" + target_playlist_name_ + "'...");
        if(!spotify_worker_.Run([=] {
            SpotifyReplaceResult result;
            bool ok = spotify_client_.ExecuteReplaceItems(playlist_id, pending_replace_uris_, expected_snapshot, result);
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
        }))
            SetSpotifyBusy(false, "PlaylistLab could not start the replace worker.");
    }

    void FinishReplaceSelected()
    {
        bool ok = pending_spotify_ok_;
        String error = pending_spotify_error_;
        SpotifyReplaceResult& result = pending_replace_result_;
        SetSpotifyBusy(false);
        if(result.observed) {
            target_tracks_ = pick(result.observed_tracks);
            target_uris_ = pick(result.observed_uris);
            target_snapshot_id_ = result.snapshot_id;
            target_loaded_ = true;
            target_items_accessible_ = true;
            int q = FindPlaylist(target_playlist_id_);
            if(q >= 0) {
                spotify_playlists_[q].items_accessible = true;
                spotify_playlists_[q].item_count = target_tracks_.GetCount();
            }
            RefreshTargetProjection();
            RefreshPlaylistProjection(q);
        }
        else if(result.partial) {
            target_loaded_ = false;
            target_tracks_.Clear();
            target_uris_.Clear();
            target_snapshot_id_.Clear();
            RefreshTargetProjection();
        }
        pending_replace_uris_.Clear();

        if(ok && result.success) {
            last_notice_ = Format("Spotify replacement verified: target now exactly matches %d Working item%s.",
                                  result.written_count, result.written_count == 1 ? "" : "s");
            UpdateSummary();
            StartTrackArtworkCache();
            return;
        }
        if(result.stale)
            last_notice_ = "Replace cancelled because the Spotify target changed. Inspect the refreshed target before trying again.";
        else if(result.partial)
            last_notice_ = "Spotify replace may have partially changed the target. Inspect the refreshed/observed target before any retry.";
        else
            last_notice_ = error.IsEmpty() ? "Spotify replacement failed before a verified final state." : error;
        UpdateSummary();
        Exclamation(last_notice_);
    }

    void ShowReplacePreview()
    {
        if(!target_loaded_ || !target_editable_) {
            Exclamation("Select an editable Spotify playlist before using Replace.");
            return;
        }
        Vector<String> desired;
        String error;
        if(!GetWorkingPublishUris(desired, error)) {
            Exclamation(error);
            return;
        }
        if(target_snapshot_id_.IsEmpty()) {
            Exclamation("Reload the target before replacing it; PlaylistLab requires a current snapshot.");
            return;
        }
        if(target_uris_ == desired) {
            last_notice_ = "Replace is unnecessary: the selected Spotify playlist already exactly matches Working Playlist.";
            UpdateSummary();
            return;
        }

        int removed = CountOccurrenceDifference(target_uris_, desired);
        int added = CountOccurrenceDifference(desired, target_uris_);
        Vector<UiModelItem> rows;
        rows.Reserve(working_document_.tracks.GetCount());
        for(int i = 0; i < working_document_.tracks.GetCount(); ++i) {
            UiModelItem& row = rows.Add();
            row.text = Format("%d. %s", i + 1, TrackDisplayTitle(working_document_.tracks[i]));
            row.description = TrackDisplayDescription(working_document_.tracks[i]);
            row.right_text = "FINAL";
        }
        String context = "DESTRUCTIVE REPLACE\nTarget: " + target_playlist_name_;
        String summary = Format("Current %d  •  Final %d  •  %d current occurrence%s removed  •  %d new occurrence%s",
                                target_uris_.GetCount(), desired.GetCount(), removed, removed == 1 ? "" : "s",
                                added, added == 1 ? "" : "s");
        String detail = "Spotify will overwrite the target's entire item sequence with this exact Working order. A fresh snapshot preflight and exact final readback are still required.";
        PreviewDialog dialog("PlaylistLab Replace Preview", context, summary, detail, rows,
                             "Replace Selected Playlist", true);
        if(dialog.Choose() != IDYES) {
            last_notice_ = "Replace preview closed. Spotify was not modified.";
            UpdateSummary();
            return;
        }
        String prompt = Format("REPLACE '%s' with the exact %d-item Working Playlist?\n\nThis is destructive: items not present in Working will be removed. PlaylistLab will refuse if the target changed since it was loaded.",
                               target_playlist_name_, desired.GetCount());
        if(!PromptYesNo(prompt)) {
            last_notice_ = "Replace cancelled. Spotify was not modified.";
            UpdateSummary();
            return;
        }
        StartReplaceSelected(desired);
    }

    void StartPublishPreview(PlaylistPublishPreview& preview)
    {
        if(!target_editable_) {
            Exclamation("The selected Spotify playlist is not editable by the current account.");
            return;
        }
        if(!PrepareSpotifyWorker())
            return;
        if(target_playlist_id_.IsEmpty() || target_snapshot_id_.IsEmpty()) {
            Exclamation("Reload the Spotify target before publishing; PlaylistLab requires a target snapshot.");
            return;
        }
        pending_publish_preview_.Clear();
        CopyPublishPreview(pending_publish_preview_.Create(), preview);
        pending_publish_result_.Clear();
        String playlist_id = target_playlist_id_;
        String expected_snapshot = target_snapshot_id_;
        PlaylistPublishPreview *job = ~pending_publish_preview_;
        SetSpotifyBusy(true, Format("Applying exact preview to '%s'...", target_playlist_name_));

        if(!spotify_worker_.Run([=] {
            SpotifyPublishResult result;
            bool ok = spotify_client_.ExecutePublishPreview(playlist_id, *job, expected_snapshot, result);
            String error = ok ? String() : spotify_client_.GetLastError();
            {
                GuiLock __;
                pending_publish_result_.Clear();
                pending_publish_result_.success = result.success;
                pending_publish_result_.stale = result.stale;
                pending_publish_result_.partial = result.partial;
                pending_publish_result_.observed = result.observed;
                pending_publish_result_.added_count = result.added_count;
                pending_publish_result_.move_count = result.move_count;
                pending_publish_result_.snapshot_id = result.snapshot_id;
                pending_publish_result_.error = result.error;
                pending_publish_result_.observed_tracks = pick(result.observed_tracks);
                pending_publish_result_.observed_uris = pick(result.observed_uris);
                pending_spotify_ok_ = ok;
                pending_spotify_error_ = error;
            }
            PostCallback([=] { FinishPublishPreview(); });
        })) {
            pending_publish_preview_.Clear();
            SetSpotifyBusy(false, "PlaylistLab could not start the Spotify apply worker thread.");
        }
    }

    void FinishPublishPreview()
    {
        bool ok = pending_spotify_ok_;
        String error = pending_spotify_error_;
        SpotifyPublishResult& result = pending_publish_result_;
        SetSpotifyBusy(false);
        if(result.observed) {
            target_tracks_ = pick(result.observed_tracks);
            target_uris_ = pick(result.observed_uris);
            target_snapshot_id_ = result.snapshot_id;
            target_loaded_ = true;
            target_items_accessible_ = true;
            int q = FindPlaylist(target_playlist_id_);
            if(q >= 0) {
                spotify_playlists_[q].items_accessible = true;
                spotify_playlists_[q].item_count = target_tracks_.GetCount();
            }
            RefreshTargetProjection();
            RefreshPlaylistProjection(q);
            StartTrackArtworkCache();
        }
        else if(result.partial) {
            target_loaded_ = false;
            target_tracks_.Clear();
            target_uris_.Clear();
            target_snapshot_id_.Clear();
            RefreshTargetProjection();
        }
        pending_publish_preview_.Clear();

        if(ok && result.success) {
            last_notice_ = Format("Spotify Apply verified: %d added / %d move%s. No items were deleted.",
                                  result.added_count, result.move_count, result.move_count == 1 ? "" : "s");
            UpdateSummary();
            return;
        }
        if(result.stale)
            last_notice_ = "Apply cancelled because the Spotify target changed. Inspect the refreshed target and create a new preview.";
        else if(result.partial)
            last_notice_ = Format("Spotify Apply stopped after %d add / %d move%s. Inspect the refreshed target before retrying.",
                                  result.added_count, result.move_count, result.move_count == 1 ? "" : "s");
        else
            last_notice_ = error.IsEmpty() ? "Spotify Apply failed before a verified mutation completed." : error;
        UpdateSummary();
        Exclamation(last_notice_);
    }

    void ShowPreview()
    {
        if(!target_loaded_) {
            Exclamation("Choose an accessible Spotify target playlist before previewing Apply.");
            return;
        }
        if(working_document_.tracks.IsEmpty()) {
            Exclamation("Build the Working Playlist before previewing Apply.");
            return;
        }

        PlaylistPublishPreview p = BuildPlaylistPublishPreview(working_document_, target_uris_, order_mode_value_);
        VectorMap<String, int> target_counts;
        for(const String& uri : target_uris_) {
            int q = target_counts.Find(uri);
            if(q < 0) target_counts.Add(uri, 1); else target_counts[q]++;
        }
        VectorMap<String, int> seen;
        Vector<UiModelItem> rows;
        rows.Reserve(p.reorder_plan.desired_uris.GetCount());
        for(int i = 0; i < p.reorder_plan.desired_uris.GetCount(); i++) {
            const String& uri = p.reorder_plan.desired_uris[i];
            int seen_q = seen.Find(uri), occurrence = 0;
            if(seen_q < 0) seen.Add(uri, 1); else { occurrence = seen[seen_q]; seen[seen_q]++; }
            int target_q = target_counts.Find(uri);
            int original_count = target_q < 0 ? 0 : target_counts[target_q];
            UiModelItem& row = rows.Add();
            row.text = Format("%d. %s", i + 1, DisplayTitleForUri(uri));
            row.description = DisplayDescriptionForUri(uri);
            row.right_text = uri.StartsWith("playlistlab:unavailable:") ? "KEEP" : occurrence >= original_count ? "ADD" : "KEEP";
        }

        String context = "Source/reference: " + (working_source_.IsEmpty() ? String("Working Playlist") : working_source_) +
                         "\nTarget: " + target_playlist_name_ + "  •  " + OrderModeText(order_mode_value_);
        String summary = Format("%d working  •  %d publishable  •  %d blocked  •  %d additions  •  %d reorders",
                                p.reference_count, p.publishable_count, p.GetBlockingCount(),
                                p.add_uris.GetCount(), p.reorder_plan.moves.GetCount());
        String detail;
        bool snapshot_ready = !target_snapshot_id_.IsEmpty();
        if(!target_editable_)
            detail = "READ ONLY — comparison is visible, but this target cannot be modified by the current Spotify account.";
        else if(!p.CanPublish())
            detail = Format("BLOCKED — review %d, missing %d, unresolved %d, invalid URI %d. Resolve the Working Playlist before applying.",
                            p.review_count, p.missing_count, p.unresolved_count, p.invalid_uri_count);
        else if(!snapshot_ready)
            detail = "BLOCKED — target snapshot evidence is missing. Reload the target before applying.";
        else if(p.IsNoOp())
            detail = "READY — target already matches the exact Working Playlist plan. No Spotify mutation is required.";
        else
            detail = "READY — Apply executes this exact preview only after fresh target/snapshot preflight and verifies the final playlist. Apply never deletes target items.";

        bool can_publish = target_editable_ && p.CanPublish() && snapshot_ready && !p.IsNoOp();
        PreviewDialog dialog("PlaylistLab Apply Preview", context, summary, detail, rows,
                             "Apply Exact Preview", can_publish);
        int action = dialog.Choose();
        if(action == IDYES) {
            String prompt = Format("Apply this exact preview to '%s'?\n\nSource/reference: %s\n%d addition%s and %d reorder%s will be sent. No items will be deleted.",
                                   target_playlist_name_,
                                   working_source_.IsEmpty() ? "Working Playlist" : ~working_source_,
                                   p.add_uris.GetCount(), p.add_uris.GetCount() == 1 ? "" : "s",
                                   p.reorder_plan.moves.GetCount(), p.reorder_plan.moves.GetCount() == 1 ? "" : "s");
            if(PromptYesNo(prompt)) {
                StartPublishPreview(p);
                return;
            }
            last_notice_ = "Apply cancelled. Spotify was not modified.";
            UpdateSummary();
            return;
        }
        last_notice_ = !target_editable_ ? "Read-only preview inspected. Spotify was not modified."
                     : p.CanPublish() ? "Apply preview inspected. Spotify was not modified."
                                      : "Apply preview remains blocked by unresolved Working Playlist rows.";
        UpdateSummary();
    }

private:
    PlaylistDocument imported_document_;
    PlaylistDocument working_document_;
    String imported_source_;
    String working_source_;
    String last_notice_ = "Choose a Spotify Client profile, load playlists, then add tracks into Working Playlist.";

    Vector<SpotifyClientProfile> client_profiles_;
    int selected_profile_ = -1;
    bool rebuilding_profiles_ = false;

    SpotifyAuth spotify_auth_;
    SpotifyClient spotify_client_;
    Thread spotify_worker_;
    bool spotify_busy_ = false;
    bool pending_spotify_ok_ = false;
    String pending_spotify_error_;
    int pending_spotify_status_ = 0;

    Thread artwork_worker_;
    bool artwork_busy_ = false;
    Vector<String> artwork_job_ids_;
    Vector<String> artwork_job_urls_;
    Vector<Image> artwork_result_images_;

    Thread track_artwork_worker_;
    Vector<String> track_artwork_job_keys_;
    Vector<String> track_artwork_job_urls_;
    Vector<Image> track_artwork_result_images_;
    VectorMap<String, Image> track_images_;
    Index<String> track_artwork_attempted_;

    Vector<SpotifyPlaylistInfo> spotify_playlists_;
    Vector<Image> playlist_images_;
    bool rebuilding_playlist_model_ = false;

    Vector<SpotifyTrack> target_tracks_;
    Vector<String> target_uris_;
    String target_playlist_id_;
    String target_playlist_name_;
    String target_snapshot_id_;
    String target_spotify_url_;
    bool target_editable_ = false;
    bool target_items_accessible_ = false;
    bool target_loaded_ = false;
    String last_playlist_id_;
    PlaylistOrderMode order_mode_value_ = ORDER_REFERENCE_SLOTS;

    Vector<SpotifyTrack> pending_find_tracks_;
    One<PlaylistDocument> pending_import_resolution_;
    One<TrackEntry> pending_resolution_;
    int pending_resolution_index_ = -1;
    One<PlaylistPublishPreview> pending_publish_preview_;
    SpotifyPublishResult pending_publish_result_;
    SpotifyReplaceResult pending_replace_result_;
    Vector<String> pending_replace_uris_;
    Vector<String> pending_create_uris_;
    SpotifyPlaylistInfo pending_created_playlist_;
    int pending_create_added_ = 0;

    UiTitleCard header_;
    UiPanel library_panel_, spotify_panel_, imported_panel_, working_panel_, rail_panel_;

    UiLabel library_heading_, library_hint_;
    UiDropdown profile_selector_;
    UiButton profile_add_, profile_edit_, profile_delete_, refresh_spotify_;
    UiListModel playlist_model_;
    UiItemRenderImage playlist_renderer_;
    UiList playlist_list_;

    UiLabel spotify_heading_, spotify_meta_;
    UiButton spotify_add_selected_, spotify_add_all_, open_spotify_;
    UiListModel target_track_model_;
    PlaylistTransferList target_track_list_;

    UiLabel imported_heading_, imported_meta_;
    UiButton import_csv_, paste_text_, imported_export_;
    UiLineEdit imported_find_;
    UiButton imported_find_button_, resolve_imported_, review_imported_;
    UiButton imported_add_selected_, imported_add_all_, imported_remove_, imported_clear_;
    UiListModel imported_model_;
    PlaylistTransferList imported_list_;

    UiLabel working_heading_, working_meta_, working_hint_;
    UiSplitButton working_destination_;
    UiButton working_remove_, working_clear_, working_export_;
    UiListModel working_model_;
    PlaylistTransferList working_list_;
    UiItemRenderImage track_renderer_;

    UiLabel placement_heading_;
    UiDropdown order_mode_;
    UiLabel selection_heading_, selection_title_, selection_artist_, selection_state_;
    UiButton resolve_working_, review_working_;
    UiLabel publish_heading_, preview_context_, preview_state_, notice_;
    UiButton preview_;
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
