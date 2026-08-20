#ifdef flagMAIN

#include <CtrlLib/CtrlLib.h>
#include <Ui/Ui.h>
#include "PlaylistIO.h"
#include "PlaylistPlanner.h"
#include "SpotifyClient.h"
#include "SpotifyImageCache.h"

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
    else
        album = entry.requested_album;

    String out = artist;
    if(!album.IsEmpty()) {
        if(!out.IsEmpty())
            out << "  •  ";
        out << album;
    }
    if(out.IsEmpty() && !entry.ResolvedUri().IsEmpty())
        out = entry.ResolvedUri();
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
    return mode == ORDER_REFERENCE_FIRST ? "Reference First" : "Keep Target Slots";
}

bool IsSpotifyPublishableUri(const String& uri)
{
    return uri.StartsWith("spotify:track:") || uri.StartsWith("spotify:episode:");
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

    virtual void Paint(Draw& w) override
    {
        w.DrawRect(GetSize(), AppBackground());
    }

    virtual void Layout() override
    {
        Rect rc = GetSize();
        int margin = DPI(14);
        int gap = DPI(8);
        int button_h = DPI(34);
        int button_w = DPI(96);
        int y = max(margin, rc.GetHeight() - margin - button_h);

        list_.SetRect(margin, margin,
                      max(0, rc.GetWidth() - margin * 2),
                      max(0, y - margin - gap));
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
    UiList      list_;
    UiButton    ok_, cancel_;
};

class PreviewDialog : public TopWindow {
public:
    typedef PreviewDialog CLASSNAME;

    PreviewDialog(const String& target,
                  const String& summary,
                  const String& detail,
                  const Vector<UiModelItem>& rows,
                  bool can_publish)
    {
        Title("PlaylistLab Publish Preview");
        Sizeable().Zoomable();
        SetRect(0, 0, DPI(820), DPI(600));

        Add(target_);
        Add(summary_);
        Add(detail_);
        Add(list_);
        Add(publish_);
        Add(close_);

        target_.SetText(target);
        summary_.SetText(summary);
        detail_.SetText(detail);
        target_.SetCustomStyle(UiTheme::ResolveLabel(APP_THEME, APP_MODE, UiLabelRole::Title));
        summary_.SetCustomStyle(UiTheme::ResolveLabel(APP_THEME, APP_MODE, UiLabelRole::Body));
        detail_.SetCustomStyle(UiTheme::ResolveLabel(APP_THEME, APP_MODE, UiLabelRole::Caption));

        publish_.SetText("Publish to Spotify");
        publish_.Enable(can_publish);
        close_.SetText("Close");
        publish_.SetCustomStyle(UiTheme::ResolveButton(APP_THEME, APP_MODE, UiButtonRole::Accent));
        close_.SetCustomStyle(UiTheme::ResolveButton(APP_THEME, APP_MODE, UiButtonRole::Subtle));
        publish_.WhenAction = [=] { AcceptBreak(IDYES); };
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
        list_.SetCustomStyle(style);
    }

    virtual void Paint(Draw& w) override
    {
        w.DrawRect(GetSize(), AppBackground());
    }

    virtual void Layout() override
    {
        Rect rc = GetSize();
        int margin = DPI(16);
        int gap = DPI(7);
        int y = margin;
        int w = max(0, rc.GetWidth() - margin * 2);

        target_.SetRect(margin, y, w, DPI(28)); y += DPI(32);
        summary_.SetRect(margin, y, w, DPI(24)); y += DPI(28);
        detail_.SetRect(margin, y, w, DPI(44)); y += DPI(50);

        int button_h = DPI(36);
        int close_w = DPI(96);
        int publish_w = DPI(154);
        int button_y = max(y, rc.GetHeight() - margin - button_h);
        list_.SetRect(margin, y, w, max(0, button_y - y - gap));
        close_.SetRect(max(margin, rc.GetWidth() - margin - close_w), button_y, close_w, button_h);
        publish_.SetRect(max(margin, rc.GetWidth() - margin - close_w - gap - publish_w),
                         button_y, publish_w, button_h);
    }

    int Choose()
    {
        return Execute();
    }

private:
    UiListModel model_;
    UiList      list_;
    UiLabel     target_, summary_, detail_;
    UiButton    publish_, close_;
};

class PlaylistLabWindow : public TopWindow {
public:
    typedef PlaylistLabWindow CLASSNAME;

    PlaylistLabWindow()
        : spotify_client_(spotify_auth_)
    {
        Title("PlaylistLab");
        Sizeable().Zoomable();
        SetRect(0, 0, DPI(1280), DPI(820));
        SetMinSize(Size(DPI(900), DPI(640)));

        LoadUiState();
        BuildHeader();
        BuildBody();
        ApplyTheme();
        ConnectEvents();
        RefreshPlaylistProjection();
        RefreshTargetProjection();
        RefreshReferenceProjection();

        if(spotify_auth_.HasClientId() && spotify_auth_.HasRefreshToken())
            PostCallback([=] { StartLoadPlaylists(false); });
    }

    ~PlaylistLabWindow()
    {
        if(spotify_worker_.IsOpen())
            spotify_worker_.Wait();
        if(artwork_worker_.IsOpen())
            artwork_worker_.Wait();
    }

    virtual void Paint(Draw& w) override
    {
        w.DrawRect(GetSize(), AppBackground());
    }

    virtual void Close() override
    {
        if(spotify_busy_) {
            last_notice_ = "Finish the current Spotify operation before closing PlaylistLab.";
            UpdateSummary();
            return;
        }
        SaveUiState();
        TopWindow::Close();
    }

    virtual void Layout() override
    {
        Rect rc = GetSize();
        const int margin = DPI(12);
        const int gap = DPI(10);
        const int header_h = DPI(76);

        header_.SetRect(margin, margin, max(0, rc.GetWidth() - margin * 2), header_h);

        int top = margin + header_h + gap;
        int content_h = max(0, rc.GetHeight() - top - margin);
        int content_w = max(0, rc.GetWidth() - margin * 2);

        int library_w = min(DPI(310), max(DPI(230), content_w * 23 / 100));
        int rail_w = min(DPI(285), max(DPI(215), content_w * 21 / 100));
        int center_w = max(DPI(300), content_w - library_w - rail_w - gap * 2);

        library_panel_.SetRect(margin, top, library_w, content_h);
        target_panel_.SetRect(margin + library_w + gap, top, center_w,
                              max(0, (content_h - gap) * 46 / 100));
        int reference_top = target_panel_.GetRect().bottom + gap;
        reference_panel_.SetRect(margin + library_w + gap, reference_top, center_w,
                                 max(0, top + content_h - reference_top));
        rail_panel_.SetRect(margin + library_w + gap + center_w + gap,
                            top, rail_w, content_h);

        LayoutLibrary();
        LayoutTarget();
        LayoutReference();
        LayoutRail();
    }

private:
    String UiStatePath() const
    {
        return ConfigFile("playlistlab.ui.json");
    }

    void LoadUiState()
    {
        String json = LoadFile(UiStatePath());
        if(json.IsEmpty())
            return;
        try {
            Value parsed = ParseJSON(json);
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

    void BuildHeader()
    {
        Add(header_);
        header_.SetTitle("PlaylistLab")
               .SetSubTitle("Spotify library  •  selected playlist  •  reference order")
               .ShowTitleLine(false)
               .SetContentInset(DPI(8))
               .SetContentCell(header_actions_);

        header_actions_.SetGap(DPI(6)).SetInset(0).SetAlignItems(UiCrossAlign::Center);
        header_actions_.AddSpacer(1).Expand(1);

        import_csv_.SetText("Import CSV");
        paste_text_.SetText("Paste Text");
        export_csv_.SetText("Export CSV");
        clear_.SetText("Clear Reference");

        header_actions_.Add(import_csv_).Fixed(DPI(94));
        header_actions_.Add(paste_text_).Fixed(DPI(94));
        header_actions_.Add(export_csv_).Fixed(DPI(94));
        header_actions_.Add(clear_).Fixed(DPI(112));
    }

    void BuildBody()
    {
        Add(library_panel_);
        Add(target_panel_);
        Add(reference_panel_);
        Add(rail_panel_);

        library_panel_.Add(library_heading_);
        library_panel_.Add(library_hint_);
        library_panel_.Add(client_id_);
        library_panel_.Add(refresh_spotify_);
        library_panel_.Add(playlist_list_);

        target_panel_.Add(target_heading_);
        target_panel_.Add(target_meta_);
        target_panel_.Add(use_as_reference_);
        target_panel_.Add(open_spotify_);
        target_panel_.Add(target_track_list_);

        reference_panel_.Add(reference_heading_);
        reference_panel_.Add(reference_meta_);
        reference_panel_.Add(track_list_);

        rail_panel_.Add(placement_heading_);
        rail_panel_.Add(order_mode_);
        rail_panel_.Add(selection_heading_);
        rail_panel_.Add(selection_title_);
        rail_panel_.Add(selection_artist_);
        rail_panel_.Add(selection_state_);
        rail_panel_.Add(resolve_selected_);
        rail_panel_.Add(review_candidate_);
        rail_panel_.Add(publish_heading_);
        rail_panel_.Add(preview_state_);
        rail_panel_.Add(preview_);
        rail_panel_.Add(notice_);

        library_heading_.SetText("SPOTIFY PLAYLISTS");
        library_hint_.SetText("Choose a playlist; its current tracks appear beside it.");
        client_id_.SetText("Client ID...");
        refresh_spotify_.SetText("Refresh");

        target_heading_.SetText("SELECTED SPOTIFY PLAYLIST");
        target_meta_.SetText("No playlist selected.");
        use_as_reference_.SetText("Use as Reference");
        open_spotify_.SetText("Open Spotify");

        reference_heading_.SetText("REFERENCE / WORKING LIST");
        reference_meta_.SetText("Import CSV, paste text, or use the selected Spotify playlist.");

        placement_heading_.SetText("PLACEMENT");
        order_mode_.Add("Keep Target Slots", (int)ORDER_REFERENCE_SLOTS);
        order_mode_.Add("Reference First", (int)ORDER_REFERENCE_FIRST);
        order_mode_.Select(order_mode_value_ == ORDER_REFERENCE_FIRST ? 1 : 0);
        order_mode_.SetPopupMaxItems(2);

        selection_heading_.SetText("REFERENCE SELECTION");
        selection_title_.SetText("No reference track selected");
        resolve_selected_.SetText("Resolve Selected");
        review_candidate_.SetText("Review Candidate");

        publish_heading_.SetText("PUBLISH PREVIEW");
        preview_state_.SetText("Select a Spotify playlist and load a reference list.");
        preview_.SetText("Preview Changes");
        notice_.SetText(last_notice_);

        playlist_list_.SetModel(playlist_model_)
                      .SetItemRender(playlist_renderer_)
                      .EnableRenameOnDblClick(false)
                      .EnableDragReorder(false)
                      .ShowDragHandle(false);

        target_track_list_.SetModel(target_track_model_)
                          .EnableRenameOnDblClick(false)
                          .EnableDragReorder(false)
                          .ShowDragHandle(false);

        track_list_.SetModel(reference_model_)
                   .EnableRenameOnDblClick(false)
                   .EnableDragReorder(true)
                   .EnableInternalMutation(false)
                   .ShowDragHandle(true)
                   .SetDragSide(UiAlign::RIGHT);
    }

    void ApplyTheme()
    {
        header_.SetCustomStyle(UiTheme::ResolveTitleCard(APP_THEME, APP_MODE));
        library_panel_.SetCustomStyle(UiTheme::ResolvePanel(APP_THEME, APP_MODE, UiPanelRole::Subtle));
        target_panel_.SetCustomStyle(UiTheme::ResolvePanel(APP_THEME, APP_MODE, UiPanelRole::Surface));
        reference_panel_.SetCustomStyle(UiTheme::ResolvePanel(APP_THEME, APP_MODE, UiPanelRole::Surface));
        rail_panel_.SetCustomStyle(UiTheme::ResolvePanel(APP_THEME, APP_MODE, UiPanelRole::Subtle));

        UiLabel::Style heading = UiTheme::ResolveLabel(APP_THEME, APP_MODE, UiLabelRole::Caption);
        heading.font = SansSerifZ(10).Bold();
        heading.metrics.text_font = heading.font;
        heading.metrics.use_text_font = true;
        library_heading_.SetCustomStyle(heading);
        target_heading_.SetCustomStyle(heading);
        reference_heading_.SetCustomStyle(heading);
        placement_heading_.SetCustomStyle(heading);
        selection_heading_.SetCustomStyle(heading);
        publish_heading_.SetCustomStyle(heading);

        UiLabel::Style body = UiTheme::ResolveLabel(APP_THEME, APP_MODE, UiLabelRole::Body);
        UiLabel::Style caption = UiTheme::ResolveLabel(APP_THEME, APP_MODE, UiLabelRole::Caption);
        library_hint_.SetCustomStyle(caption);
        target_meta_.SetCustomStyle(caption);
        reference_meta_.SetCustomStyle(caption);
        selection_title_.SetCustomStyle(body);
        selection_artist_.SetCustomStyle(caption);
        selection_state_.SetCustomStyle(caption);
        preview_state_.SetCustomStyle(caption);
        notice_.SetCustomStyle(caption);

        UiButton::Style standard = UiTheme::ResolveButton(APP_THEME, APP_MODE, UiButtonRole::Standard);
        UiButton::Style subtle = UiTheme::ResolveButton(APP_THEME, APP_MODE, UiButtonRole::Subtle);
        UiButton::Style accent = UiTheme::ResolveButton(APP_THEME, APP_MODE, UiButtonRole::Accent);

        import_csv_.SetCustomStyle(standard);
        paste_text_.SetCustomStyle(standard);
        export_csv_.SetCustomStyle(subtle);
        clear_.SetCustomStyle(subtle);
        client_id_.SetCustomStyle(subtle);
        refresh_spotify_.SetCustomStyle(accent);
        use_as_reference_.SetCustomStyle(standard);
        open_spotify_.SetCustomStyle(subtle);
        resolve_selected_.SetCustomStyle(standard);
        review_candidate_.SetCustomStyle(subtle);
        preview_.SetCustomStyle(accent);
        order_mode_.SetCustomStyle(UiTheme::ResolveDropdown(APP_THEME, APP_MODE));

        UiList::Style playlists = UiTheme::ResolveList(APP_THEME, APP_MODE);
        playlists.row_height = DPI(72);
        playlists.show_checks = false;
        playlists.show_icons = false;
        playlists.show_metadata_marker = false;
        playlists.right_text_as_badge = true;
        playlists.row_radius = DPI(6);
        playlists.item_spacing = DPI(2);
        playlist_list_.SetCustomStyle(playlists);

        UiList::Style target = UiTheme::ResolveList(APP_THEME, APP_MODE);
        target.row_height = DPI(44);
        target.show_checks = false;
        target.show_icons = false;
        target.show_metadata_marker = false;
        target.right_text_as_badge = false;
        target_track_list_.SetCustomStyle(target);

        UiList::Style reference = UiTheme::ResolveList(APP_THEME, APP_MODE);
        reference.row_height = DPI(44);
        reference.show_checks = false;
        reference.show_icons = false;
        reference.show_metadata_marker = true;
        reference.right_text_as_badge = true;
        reference.drag_side = UiAlign::RIGHT;
        track_list_.SetCustomStyle(reference);
    }

    void ConnectEvents()
    {
        import_csv_.WhenAction = [=] { ImportCsv(); };
        paste_text_.WhenAction = [=] { ImportClipboardText(); };
        export_csv_.WhenAction = [=] { ExportCsv(); };
        clear_.WhenAction = [=] { ClearDocument(); };
        client_id_.WhenAction = [=] { EditClientId(); };
        refresh_spotify_.WhenAction = [=] { StartLoadPlaylists(true); };
        playlist_list_.WhenSelection = [=] { OnPlaylistSelection(); };
        use_as_reference_.WhenAction = [=] { UseTargetAsReference(); };
        open_spotify_.WhenAction = [=] { OpenTargetInSpotify(); };
        resolve_selected_.WhenAction = [=] { StartResolveSelected(); };
        review_candidate_.WhenAction = [=] { ReviewSelectedCandidate(); };
        preview_.WhenAction = [=] { ShowPreview(); };
        order_mode_.WhenSelect = [=](int) {
            order_mode_value_ = order_mode_.GetSelection() == 1 ? ORDER_REFERENCE_FIRST
                                                               : ORDER_REFERENCE_SLOTS;
            SaveUiState();
            UpdateSummary();
        };
        track_list_.WhenSelection = [=] { UpdateReferenceSelection(); };
        track_list_.WhenReorderRequest = [=](UiReorderRequest& request) { ReorderDocument(request); };
    }

    void LayoutLibrary()
    {
        Rect rc = library_panel_.GetSize();
        int margin = DPI(12);
        int w = max(0, rc.GetWidth() - margin * 2);
        int y = margin;
        library_heading_.SetRect(margin, y, w, DPI(20)); y += DPI(24);
        library_hint_.SetRect(margin, y, w, DPI(34)); y += DPI(40);
        int button_h = DPI(32);
        int gap = DPI(6);
        int refresh_w = DPI(84);
        client_id_.SetRect(margin, y, max(0, w - refresh_w - gap), button_h);
        refresh_spotify_.SetRect(max(margin, rc.GetWidth() - margin - refresh_w), y, refresh_w, button_h);
        y += button_h + DPI(10);
        playlist_list_.SetRect(margin, y, w, max(0, rc.GetHeight() - y - margin));
    }

    void LayoutTarget()
    {
        Rect rc = target_panel_.GetSize();
        int margin = DPI(12);
        int gap = DPI(6);
        int w = max(0, rc.GetWidth() - margin * 2);
        int button_h = DPI(30);
        int open_w = DPI(104);
        int reference_w = DPI(124);
        int actions_w = open_w + reference_w + gap;

        target_heading_.SetRect(margin, margin, max(0, w - actions_w - gap), DPI(20));
        use_as_reference_.SetRect(max(margin, rc.GetWidth() - margin - actions_w), margin,
                                  reference_w, button_h);
        open_spotify_.SetRect(max(margin, rc.GetWidth() - margin - open_w), margin,
                              open_w, button_h);
        int y = margin + button_h + DPI(4);
        target_meta_.SetRect(margin, y, w, DPI(22)); y += DPI(28);
        target_track_list_.SetRect(margin, y, w, max(0, rc.GetHeight() - y - margin));
    }

    void LayoutReference()
    {
        Rect rc = reference_panel_.GetSize();
        int margin = DPI(12);
        int w = max(0, rc.GetWidth() - margin * 2);
        int y = margin;
        reference_heading_.SetRect(margin, y, w, DPI(20)); y += DPI(23);
        reference_meta_.SetRect(margin, y, w, DPI(22)); y += DPI(28);
        track_list_.SetRect(margin, y, w, max(0, rc.GetHeight() - y - margin));
    }

    void LayoutRail()
    {
        Rect rc = rail_panel_.GetSize();
        int margin = DPI(14);
        int gap = DPI(7);
        int w = max(0, rc.GetWidth() - margin * 2);
        int y = margin;

        placement_heading_.SetRect(margin, y, w, DPI(20)); y += DPI(24);
        order_mode_.SetRect(margin, y, w, DPI(34)); y += DPI(46);

        selection_heading_.SetRect(margin, y, w, DPI(20)); y += DPI(24);
        selection_title_.SetRect(margin, y, w, DPI(28)); y += DPI(30);
        selection_artist_.SetRect(margin, y, w, DPI(24)); y += DPI(24);
        selection_state_.SetRect(margin, y, w, DPI(24)); y += DPI(30);
        resolve_selected_.SetRect(margin, y, w, DPI(34)); y += DPI(34) + gap;
        review_candidate_.SetRect(margin, y, w, DPI(34)); y += DPI(48);

        publish_heading_.SetRect(margin, y, w, DPI(20)); y += DPI(24);
        preview_state_.SetRect(margin, y, w, DPI(58)); y += DPI(64);
        preview_.SetRect(margin, y, w, DPI(36));

        int notice_h = DPI(72);
        notice_.SetRect(margin, max(y + DPI(48), rc.GetHeight() - margin - notice_h), w, notice_h);
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
        int warning_count = result.warnings.GetCount();
        document_ = pick(result.document);
        document_.dirty = false;
        reference_source_ = "CSV: " + GetFileName(path);
        last_notice_ = warning_count
                     ? Format("Imported with %d warning%s.", warning_count, warning_count == 1 ? "" : "s")
                     : String("CSV loaded as the reference list. Spotify was not modified.");
        RefreshReferenceProjection(document_.tracks.IsEmpty() ? -1 : 0);
    }

    void ImportClipboardText()
    {
        String text = ReadClipboardText();
        if(TrimBoth(text).IsEmpty()) {
            Exclamation("The clipboard does not contain a song list.");
            return;
        }

        PlaylistImportResult result = ImportPlaylistText(text, "Clipboard");
        int warning_count = result.warnings.GetCount();
        document_ = pick(result.document);
        document_.dirty = !document_.tracks.IsEmpty();
        reference_source_ = "Clipboard";
        last_notice_ = warning_count
                     ? Format("Clipboard import produced %d warning%s.", warning_count, warning_count == 1 ? "" : "s")
                     : String("Clipboard text loaded as the reference list.");
        RefreshReferenceProjection(document_.tracks.IsEmpty() ? -1 : 0);
    }

    void ExportCsv()
    {
        if(document_.tracks.IsEmpty()) {
            Exclamation("There is no reference list to export.");
            return;
        }

        String path = SelectFileSaveAs("CSV files\t*.csv\nAll files\t*.*");
        if(path.IsEmpty())
            return;
        if(GetFileExt(path).IsEmpty())
            path << ".csv";

        if(!SaveFile(path, ExportPlaylistCsv(document_))) {
            Exclamation("PlaylistLab could not write the CSV file.");
            return;
        }

        document_.source_path = path;
        document_.name = GetFileTitle(path);
        document_.dirty = false;
        reference_source_ = "CSV: " + GetFileName(path);
        last_notice_ = "Reference list exported. Spotify was not modified.";
        UpdateSummary();
    }

    void ClearDocument()
    {
        if(document_.dirty && !PromptYesNo("Discard the current reference-list changes?"))
            return;
        document_.Clear();
        reference_source_.Clear();
        last_notice_ = "Reference cleared. Import, paste, or use a Spotify playlist as the reference.";
        RefreshReferenceProjection();
    }

    void UseTargetAsReference()
    {
        if(!target_loaded_)
            return;
        if(document_.dirty && !PromptYesNo("Replace the modified reference list with the selected Spotify playlist?"))
            return;

        document_.Clear();
        document_.name = target_playlist_name_;
        document_.tracks.Reserve(target_tracks_.GetCount());
        for(const SpotifyTrack& track : target_tracks_) {
            TrackEntry entry;
            entry.requested_title = track.title;
            entry.requested_artist = track.artist;
            entry.requested_album = track.album;
            entry.requested_isrc = track.isrc;
            if(!track.placeholder && IsSpotifyPublishableUri(track.uri)) {
                entry.spotify_uri = track.uri;
                entry.state = TRACK_EXACT;
                entry.confidence = 100;
                entry.note = "Loaded directly from Spotify";
            }
            else {
                entry.state = TRACK_MISSING;
                entry.note = "Unavailable Spotify playlist position";
            }
            document_.tracks.Add(pick(entry));
        }
        document_.dirty = false;
        reference_source_ = "Spotify: " + target_playlist_name_;
        last_notice_ = Format("'%s' is now the reference list. Select another Spotify playlist to compare or publish against it.",
                              target_playlist_name_);
        RefreshReferenceProjection(document_.tracks.IsEmpty() ? -1 : 0);
    }

    void ReorderDocument(UiReorderRequest& request)
    {
        if(spotify_busy_) {
            request.accept = false;
            return;
        }

        int target = request.before;
        if(!document_.MoveTrack(request.from, request.before)) {
            request.accept = false;
            return;
        }

        request.handled = true;
        if(target > request.from)
            target--;
        last_notice_ = "Reference order changed locally. Inspect Preview before publishing.";
        RefreshReferenceProjection(target);
    }

    void RefreshReferenceProjection(int selected = -1)
    {
        Vector<UiModelItem> rows;
        rows.Reserve(document_.tracks.GetCount());
        for(int i = 0; i < document_.tracks.GetCount(); i++) {
            const TrackEntry& entry = document_.tracks[i];
            UiModelItem item;
            item.text = Format("%d. %s", i + 1, TrackDisplayTitle(entry));
            item.description = TrackDisplayDescription(entry);
            item.right_text = TrackMatchStateText(entry.state);
            item.data = i;
            item.has_metadata = true;
            item.metadata_color = MatchStateColor(entry.state);
            rows.Add(pick(item));
        }

        reference_model_.Clear();
        if(!rows.IsEmpty())
            reference_model_.AddRange(rows);

        if(selected >= 0 && selected < document_.tracks.GetCount())
            track_list_.SetCursor(selected);
        UpdateReferenceSelection();
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
            row.right_text = Format("%d  %s", playlist.item_count, playlist.editable ? "EDIT" : "READ");
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
            row.text = Format("%d. %s", i + 1,
                              track.title.IsEmpty() ? String("Untitled Spotify item") : track.title);
            row.description = SpotifyTrackDescription(track);
            row.right_text = track.placeholder ? String("UNAVAILABLE") : DurationText(track.duration_ms);
            row.data = i;
            rows.Add(pick(row));
        }

        target_track_model_.Clear();
        if(!rows.IsEmpty())
            target_track_model_.AddRange(rows);
        UpdateSummary();
    }

    void UpdateReferenceSelection()
    {
        int index = track_list_.GetCursor();
        if(index < 0 || index >= document_.tracks.GetCount()) {
            selection_title_.SetText("No reference track selected");
            selection_artist_.SetText(Null);
            selection_state_.SetText(Null);
            UpdateActionState();
            return;
        }

        const TrackEntry& entry = document_.tracks[index];
        selection_title_.SetText(TrackDisplayTitle(entry));
        selection_artist_.SetText(entry.ResolvedArtist().IsEmpty() ? "Artist: -" : "Artist: " + entry.ResolvedArtist());
        String state = "State: " + TrackMatchStateText(entry.state);
        if(!entry.ResolvedUri().IsEmpty())
            state << "  •  Spotify linked";
        selection_state_.SetText(state);
        UpdateActionState();
    }

    void UpdateSummary()
    {
        int total = document_.tracks.GetCount();
        int resolved = document_.GetResolvedCount();
        int review = document_.GetReviewCount();
        int missing = document_.GetMissingCount();

        String subtitle;
        if(!spotify_playlists_.IsEmpty())
            subtitle << spotify_playlists_.GetCount() << " Spotify playlists";
        else
            subtitle << (spotify_auth_.HasRefreshToken() ? "Spotify ready to refresh" : "Spotify not connected");
        if(target_loaded_)
            subtitle << "  •  " << target_playlist_name_ << " selected";
        subtitle << "  •  " << total << " reference tracks";
        if(document_.dirty)
            subtitle << "  •  modified";
        if(spotify_busy_)
            subtitle << "  •  Spotify working";
        header_.SetSubTitle(subtitle);

        if(target_playlist_id_.IsEmpty())
            target_meta_.SetText("No playlist selected. Choose one from the Spotify library.");
        else if(!target_loaded_)
            target_meta_.SetText("Loading " + target_playlist_name_ + "...");
        else
            target_meta_.SetText(Format("%s  •  %d tracks  •  %s",
                                        target_playlist_name_, target_tracks_.GetCount(),
                                        target_editable_ ? "editable target" : "read-only target"));

        String ref_name = document_.name.IsEmpty() ? String("Reference list") : document_.name;
        String ref_source = reference_source_.IsEmpty() ? String("local / unsaved") : reference_source_;
        reference_meta_.SetText(Format("%s  •  %d tracks / %d publishable  •  %s",
                                       ref_name, total, resolved, ref_source));

        if(target_loaded_ && total > 0) {
            PlaylistPublishPreview p = BuildPlaylistPublishPreview(document_, target_uris_, order_mode_value_);
            if(!target_editable_)
                preview_state_.SetText(Format("Read-only target. Preview: %d add / %d move / %d blocked.",
                                             p.add_uris.GetCount(), p.reorder_plan.moves.GetCount(),
                                             p.GetBlockingCount()));
            else
                preview_state_.SetText(Format("%s: %d add / %d move / %d blocked.",
                                             OrderModeText(order_mode_value_), p.add_uris.GetCount(),
                                             p.reorder_plan.moves.GetCount(), p.GetBlockingCount()));
        }
        else if(!target_loaded_)
            preview_state_.SetText("Choose a Spotify playlist to establish the target.");
        else
            preview_state_.SetText("Load a reference list before previewing changes.");

        if(review || missing)
            preview_state_.SetText(preview_state_.GetText() + Format("  Reference needs %d review / %d missing.", review, missing));

        notice_.SetText(last_notice_.IsEmpty()
                        ? "Spotify writes are never automatic. Preview and confirm explicitly."
                        : last_notice_);
        UpdateActionState();
    }

    void UpdateActionState()
    {
        int total = document_.tracks.GetCount();
        int index = track_list_.GetCursor();
        const TrackEntry *entry = index >= 0 && index < total ? &document_.tracks[index] : nullptr;
        bool idle = !spotify_busy_;
        bool can_resolve = entry && (entry->state == TRACK_UNRESOLVED ||
                                     entry->state == TRACK_REVIEW ||
                                     entry->state == TRACK_MISSING);
        bool can_review = entry && !entry->candidates.IsEmpty();

        import_csv_.Enable(idle);
        paste_text_.Enable(idle);
        export_csv_.Enable(idle && total > 0);
        clear_.Enable(idle && total > 0);
        client_id_.Enable(idle);
        refresh_spotify_.Enable(idle);
        playlist_list_.Enable(idle && !spotify_playlists_.IsEmpty());
        target_track_list_.Enable(idle && target_loaded_);
        use_as_reference_.Enable(idle && target_loaded_ && !target_tracks_.IsEmpty());
        open_spotify_.Enable(idle && target_loaded_ && !target_spotify_url_.IsEmpty());
        order_mode_.Enable(idle);
        resolve_selected_.Enable(idle && can_resolve);
        review_candidate_.Enable(idle && can_review);
        preview_.Enable(idle && target_loaded_ && total > 0);
        track_list_.Enable(idle);
    }

    bool EnsureSpotifyClientId()
    {
        if(spotify_auth_.HasClientId())
            return true;

        String client_id;
        if(!EditTextNotNull(client_id, "PlaylistLab Spotify Setup", "Spotify Client ID"))
            return false;

        spotify_auth_.SetClientId(client_id);
        if(!spotify_auth_.Save()) {
            Exclamation("PlaylistLab could not save the Spotify Client ID.");
            return false;
        }
        return true;
    }

    void EditClientId()
    {
        if(spotify_busy_)
            return;
        String client_id = spotify_auth_.GetClientId();
        if(!EditTextNotNull(client_id, "PlaylistLab Spotify Setup", "Spotify Client ID"))
            return;

        bool changed = TrimBoth(client_id) != spotify_auth_.GetClientId();
        spotify_auth_.SetClientId(client_id);
        if(!spotify_auth_.Save()) {
            Exclamation("PlaylistLab could not save the Spotify Client ID.");
            return;
        }
        if(changed) {
            spotify_playlists_.Clear();
            playlist_images_.Clear();
            ClearTarget();
            last_playlist_id_.Clear();
            SaveUiState();
            RefreshPlaylistProjection();
            RefreshTargetProjection();
        }
        StartLoadPlaylists(true);
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

    void StartLoadPlaylists(bool prompt_for_client_id)
    {
        if(!spotify_auth_.HasClientId()) {
            if(!prompt_for_client_id || !EnsureSpotifyClientId())
                return;
        }
        if(!PrepareSpotifyWorker())
            return;

        SetSpotifyBusy(true, "Connecting to Spotify and loading your playlist library...");
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
        })) {
            SetSpotifyBusy(false, "PlaylistLab could not start the Spotify worker thread.");
        }
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

        playlist_images_.SetCount(spotify_playlists_.GetCount());
        for(int i = 0; i < spotify_playlists_.GetCount(); i++)
            playlist_images_[i] = SpotifyImageCache::Load("playlist-" + spotify_playlists_[i].id);

        int selected = FindPlaylist(last_playlist_id_);
        if(selected < 0 && !target_playlist_id_.IsEmpty())
            selected = FindPlaylist(target_playlist_id_);
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

        last_notice_ = Format("Loaded %d Spotify playlist%s. Choose any row to inspect its current tracks.",
                              spotify_playlists_.GetCount(), spotify_playlists_.GetCount() == 1 ? "" : "s");
        UpdateSummary();
        if(selected >= 0)
            StartLoadTarget(selected);
    }

    int FindPlaylist(const String& id) const
    {
        if(id.IsEmpty())
            return -1;
        for(int i = 0; i < spotify_playlists_.GetCount(); i++)
            if(spotify_playlists_[i].id == id)
                return i;
        return -1;
    }

    void StartArtworkCache()
    {
        if(artwork_worker_.IsOpen())
            return;

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
                images[i] = SpotifyImageCache::LoadOrFetch("playlist-" + artwork_job_ids_[i],
                                                           artwork_job_urls_[i], &ignored);
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
            int playlist_index = FindPlaylist(artwork_job_ids_[i]);
            if(playlist_index >= 0 && playlist_index < playlist_images_.GetCount())
                playlist_images_[playlist_index] = artwork_result_images_[i];
        }
        artwork_result_images_.Clear();
        int selected = playlist_list_.GetCursor();
        RefreshPlaylistProjection(selected);
    }

    void OnPlaylistSelection()
    {
        if(rebuilding_playlist_model_ || spotify_busy_)
            return;
        int index = playlist_list_.GetCursor();
        if(index < 0 || index >= spotify_playlists_.GetCount())
            return;
        if(target_loaded_ && target_playlist_id_ == spotify_playlists_[index].id)
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
        target_loaded_ = false;
        target_tracks_.Clear();
        target_uris_.Clear();
    }

    void StartLoadTarget(int playlist_index)
    {
        if(playlist_index < 0 || playlist_index >= spotify_playlists_.GetCount() || !PrepareSpotifyWorker())
            return;

        const SpotifyPlaylistInfo& playlist = spotify_playlists_[playlist_index];
        target_playlist_id_ = playlist.id;
        target_playlist_name_ = playlist.name;
        target_spotify_url_ = playlist.spotify_url;
        target_editable_ = playlist.editable;
        target_snapshot_id_.Clear();
        target_loaded_ = false;
        target_tracks_.Clear();
        target_uris_.Clear();
        RefreshTargetProjection();

        String playlist_id = target_playlist_id_;
        SetSpotifyBusy(true, "Loading '" + target_playlist_name_ + "' from Spotify...");
        if(!spotify_worker_.Run([=] {
            Vector<SpotifyTrack> found;
            String snapshot;
            bool ok = spotify_client_.GetPlaylistItems(playlist_id, found, &snapshot);
            String error = ok ? String() : spotify_client_.GetLastError();

            {
                GuiLock __;
                target_tracks_ = pick(found);
                target_snapshot_id_ = snapshot;
                pending_spotify_ok_ = ok;
                pending_spotify_error_ = error;
            }
            PostCallback([=] { FinishLoadTarget(); });
        })) {
            SetSpotifyBusy(false, "PlaylistLab could not start the Spotify worker thread.");
        }
    }

    void FinishLoadTarget()
    {
        bool ok = pending_spotify_ok_;
        String error = pending_spotify_error_;
        SetSpotifyBusy(false);

        if(!ok) {
            target_loaded_ = false;
            last_notice_ = error.IsEmpty() ? "Spotify playlist loading failed." : error;
            RefreshTargetProjection();
            Exclamation(last_notice_);
            return;
        }

        target_uris_.Clear();
        target_uris_.Reserve(target_tracks_.GetCount());
        for(const SpotifyTrack& track : target_tracks_)
            target_uris_.Add(track.uri);
        target_loaded_ = true;
        last_playlist_id_ = target_playlist_id_;
        SaveUiState();

        int q = FindPlaylist(target_playlist_id_);
        if(q >= 0)
            spotify_playlists_[q].item_count = target_tracks_.GetCount();

        last_notice_ = Format("Loaded '%s' with %d item%s. %s",
                              target_playlist_name_, target_uris_.GetCount(),
                              target_uris_.GetCount() == 1 ? "" : "s",
                              target_editable_ ? "It can be used as a guarded publish target."
                                               : "This playlist is read-only in PlaylistLab.");
        RefreshTargetProjection();
        RefreshPlaylistProjection(q);
    }

    void OpenTargetInSpotify()
    {
        if(!target_spotify_url_.IsEmpty())
            LaunchWebBrowser(target_spotify_url_);
    }

    void StartResolveSelected()
    {
        int index = track_list_.GetCursor();
        if(index < 0 || index >= document_.tracks.GetCount())
            return;
        if(!EnsureSpotifyClientId() || !PrepareSpotifyWorker())
            return;

        const TrackEntry& source = document_.tracks[index];
        String title = source.requested_title;
        String artist = source.requested_artist;
        String album = source.requested_album;
        String isrc = source.requested_isrc;
        String uri = source.spotify_uri;
        TrackMatchState state = source.state;

        pending_resolution_index_ = index;
        pending_resolution_.Clear();
        SetSpotifyBusy(true, Format("Resolving '%s' against Spotify...", TrackDisplayTitle(source)));

        if(!spotify_worker_.Run([=] {
            TrackEntry resolved;
            resolved.requested_title = title;
            resolved.requested_artist = artist;
            resolved.requested_album = album;
            resolved.requested_isrc = isrc;
            resolved.spotify_uri = uri;
            resolved.state = state;

            String error;
            bool ok = EnsureSpotifyAuthorizedWorker(error);
            if(ok && !spotify_client_.ResolveTrack(resolved)) {
                error = spotify_client_.GetLastError();
                ok = false;
            }

            {
                GuiLock __;
                pending_resolution_.Create();
                Swap(*pending_resolution_, resolved);
                pending_spotify_ok_ = ok;
                pending_spotify_error_ = error;
            }
            PostCallback([=] { FinishResolveSelected(); });
        })) {
            SetSpotifyBusy(false, "PlaylistLab could not start the Spotify worker thread.");
        }
    }

    void FinishResolveSelected()
    {
        bool ok = pending_spotify_ok_;
        String error = pending_spotify_error_;
        int index = pending_resolution_index_;
        SetSpotifyBusy(false);

        if(!ok || !pending_resolution_) {
            last_notice_ = error.IsEmpty() ? "Spotify track resolution failed." : error;
            UpdateSummary();
            Exclamation(last_notice_);
            return;
        }
        if(index < 0 || index >= document_.tracks.GetCount()) {
            pending_resolution_.Clear();
            last_notice_ = "The reference list changed before Spotify resolution completed.";
            UpdateSummary();
            return;
        }

        Swap(document_.tracks[index], *pending_resolution_);
        pending_resolution_.Clear();
        document_.dirty = true;
        TrackMatchState state = document_.tracks[index].state;
        last_notice_ = state == TRACK_REVIEW
                     ? "Spotify candidates loaded. Confirm one before this reference row becomes publishable."
                     : state == TRACK_MISSING
                       ? "Spotify did not return a usable candidate for this reference track."
                       : "Spotify resolution updated the reference list. Preview before publishing.";
        RefreshReferenceProjection(index);

        if(state == TRACK_REVIEW && !document_.tracks[index].candidates.IsEmpty())
            ReviewCandidate(index);
    }

    void ReviewSelectedCandidate()
    {
        int index = track_list_.GetCursor();
        if(index >= 0 && index < document_.tracks.GetCount())
            ReviewCandidate(index);
    }

    void ReviewCandidate(int index)
    {
        if(index < 0 || index >= document_.tracks.GetCount())
            return;
        TrackEntry& entry = document_.tracks[index];
        if(entry.candidates.IsEmpty())
            return;

        Vector<UiModelItem> rows;
        rows.Reserve(entry.candidates.GetCount());
        for(int i = 0; i < entry.candidates.GetCount(); i++) {
            const SpotifyTrack& candidate = entry.candidates[i];
            UiModelItem& row = rows.Add();
            row.text = candidate.title.IsEmpty() ? candidate.uri : candidate.title;
            row.description = candidate.artist;
            if(!candidate.album.IsEmpty()) {
                if(!row.description.IsEmpty())
                    row.description << "  •  ";
                row.description << candidate.album;
            }
            row.right_text = Format("score %d", ScoreTrackCandidate(entry, candidate));
            row.data = i;
        }

        UiChoiceDialog dialog("Confirm Spotify Candidate", rows);
        int initial = entry.selected_candidate >= 0 ? entry.selected_candidate : 0;
        int selected = dialog.Choose(initial);
        if(selected < 0) {
            last_notice_ = "Candidate review left unresolved; nothing became publishable.";
            UpdateSummary();
            return;
        }

        int score = ScoreTrackCandidate(entry, entry.candidates[selected]);
        entry.SelectCandidate(selected, TRACK_EXACT);
        entry.confidence = score;
        entry.note = "Confirmed by user";
        document_.dirty = true;
        last_notice_ = "Candidate confirmed locally. Preview is still required before any Spotify write.";
        RefreshReferenceProjection(index);
    }

    String DisplayTitleForUri(const String& uri) const
    {
        for(const TrackEntry& entry : document_.tracks)
            if(entry.ResolvedUri() == uri)
                return TrackDisplayTitle(entry);
        for(const SpotifyTrack& track : target_tracks_)
            if(track.uri == uri && !track.title.IsEmpty())
                return track.title;
        return uri;
    }

    String DisplayDescriptionForUri(const String& uri) const
    {
        for(const TrackEntry& entry : document_.tracks)
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

    void StartPublishPreview(PlaylistPublishPreview& preview)
    {
        if(!target_editable_) {
            Exclamation("The selected Spotify playlist is read-only for this account. Choose an editable target before publishing.");
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
        SetSpotifyBusy(true, Format("Publishing exact preview to '%s'...", target_playlist_name_));

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
            SetSpotifyBusy(false, "PlaylistLab could not start the Spotify publish worker thread.");
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
            int q = FindPlaylist(target_playlist_id_);
            if(q >= 0)
                spotify_playlists_[q].item_count = target_tracks_.GetCount();
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
        pending_publish_preview_.Clear();

        if(ok && result.success) {
            last_notice_ = Format("Spotify publish verified: %d added / %d move%s. No items were deleted.",
                                  result.added_count, result.move_count, result.move_count == 1 ? "" : "s");
            UpdateSummary();
            return;
        }

        if(result.stale)
            last_notice_ = "Publish cancelled because the Spotify target changed. Inspect the refreshed playlist and create a new preview.";
        else if(result.partial)
            last_notice_ = Format("Spotify publish stopped after %d add / %d move%s. Inspect the refreshed target before retrying.",
                                  result.added_count, result.move_count, result.move_count == 1 ? "" : "s");
        else
            last_notice_ = error.IsEmpty() ? "Spotify publish failed before a verified mutation completed." : error;

        UpdateSummary();
        Exclamation(last_notice_);
    }

    void ShowPreview()
    {
        if(!target_loaded_) {
            Exclamation("Choose and load a Spotify playlist before previewing.");
            return;
        }
        if(document_.tracks.IsEmpty()) {
            Exclamation("Load a reference list before previewing. Import CSV, paste text, or use a Spotify playlist as the reference.");
            return;
        }

        PlaylistPublishPreview p = BuildPlaylistPublishPreview(document_, target_uris_, order_mode_value_);
        VectorMap<String, int> target_counts;
        for(const String& uri : target_uris_) {
            int q = target_counts.Find(uri);
            if(q < 0)
                target_counts.Add(uri, 1);
            else
                target_counts[q]++;
        }
        VectorMap<String, int> seen;

        Vector<UiModelItem> rows;
        rows.Reserve(p.reorder_plan.desired_uris.GetCount());
        for(int i = 0; i < p.reorder_plan.desired_uris.GetCount(); i++) {
            const String& uri = p.reorder_plan.desired_uris[i];
            int seen_q = seen.Find(uri);
            int occurrence = 0;
            if(seen_q < 0)
                seen.Add(uri, 1);
            else {
                occurrence = seen[seen_q];
                seen[seen_q]++;
            }
            int target_q = target_counts.Find(uri);
            int original_count = target_q < 0 ? 0 : target_counts[target_q];

            UiModelItem& row = rows.Add();
            row.text = Format("%d. %s", i + 1, DisplayTitleForUri(uri));
            row.description = DisplayDescriptionForUri(uri);
            if(uri.StartsWith("playlistlab:unavailable:"))
                row.right_text = "KEEP";
            else if(occurrence >= original_count)
                row.right_text = "ADD";
            else
                row.right_text = "KEEP";
        }

        String summary = Format("%d reference  •  %d publishable  •  %d blocked  •  %d add  •  %d move",
                                p.reference_count, p.publishable_count, p.GetBlockingCount(),
                                p.add_uris.GetCount(), p.reorder_plan.moves.GetCount());
        String detail;
        bool snapshot_ready = !target_snapshot_id_.IsEmpty();
        if(!target_editable_)
            detail = "READ ONLY — this plan can be inspected, but the selected Spotify playlist cannot be modified by this account.";
        else if(!p.CanPublish())
            detail = Format("BLOCKED — review %d, missing %d, unresolved %d, invalid URI %d. Resolve every reference row before publishing.",
                            p.review_count, p.missing_count, p.unresolved_count, p.invalid_uri_count);
        else if(!snapshot_ready)
            detail = "BLOCKED — the target has no snapshot evidence. Reload the Spotify playlist before publishing.";
        else if(p.IsNoOp())
            detail = "READY — the selected Spotify playlist already matches this reference order. No mutation is required.";
        else
            detail = "READY — Publish executes this exact preview after a fresh target/snapshot preflight, then verifies the final playlist.";

        bool can_publish = target_editable_ && p.CanPublish() && snapshot_ready && !p.IsNoOp();
        PreviewDialog dialog(target_playlist_name_ + "  •  " + OrderModeText(order_mode_value_),
                             summary, detail, rows, can_publish);
        int action = dialog.Choose();
        if(action == IDYES) {
            String prompt = Format("Publish this exact preview to '%s'?\n\n%d addition%s and %d move%s will be sent. No items will be deleted.",
                                   target_playlist_name_,
                                   p.add_uris.GetCount(), p.add_uris.GetCount() == 1 ? "" : "s",
                                   p.reorder_plan.moves.GetCount(), p.reorder_plan.moves.GetCount() == 1 ? "" : "s");
            if(PromptYesNo(prompt)) {
                StartPublishPreview(p);
                return;
            }
            last_notice_ = "Publish cancelled. Spotify was not modified.";
            UpdateSummary();
            return;
        }

        last_notice_ = !target_editable_
                     ? "Read-only preview inspected. Spotify was not modified."
                     : p.CanPublish()
                       ? "Preview inspected. Spotify was not modified."
                       : "Preview is blocked until every reference row is publishable.";
        UpdateSummary();
    }

private:
    PlaylistDocument document_;
    String            reference_source_;
    String            last_notice_ = "Load Spotify, choose a playlist, then import or choose a reference list.";

    SpotifyAuth   spotify_auth_;
    SpotifyClient spotify_client_;
    Thread        spotify_worker_;
    bool          spotify_busy_ = false;
    bool          pending_spotify_ok_ = false;
    String        pending_spotify_error_;

    Thread         artwork_worker_;
    bool           artwork_busy_ = false;
    Vector<String> artwork_job_ids_;
    Vector<String> artwork_job_urls_;
    Vector<Image>  artwork_result_images_;

    Vector<SpotifyPlaylistInfo> spotify_playlists_;
    Vector<Image>               playlist_images_;
    bool                        rebuilding_playlist_model_ = false;

    Vector<SpotifyTrack> target_tracks_;
    Vector<String>       target_uris_;
    String target_playlist_id_;
    String target_playlist_name_;
    String target_snapshot_id_;
    String target_spotify_url_;
    bool   target_editable_ = false;
    bool   target_loaded_ = false;

    String last_playlist_id_;
    PlaylistOrderMode order_mode_value_ = ORDER_REFERENCE_SLOTS;

    One<TrackEntry> pending_resolution_;
    int             pending_resolution_index_ = -1;
    One<PlaylistPublishPreview> pending_publish_preview_;
    SpotifyPublishResult        pending_publish_result_;

    UiTitleCard header_;
    UiBoxLayout header_actions_ { UiDirection::H };
    UiButton import_csv_, paste_text_, export_csv_, clear_;

    UiPanel library_panel_, target_panel_, reference_panel_, rail_panel_;

    UiLabel library_heading_, library_hint_;
    UiButton client_id_, refresh_spotify_;
    UiListModel playlist_model_;
    UiItemRenderImage playlist_renderer_;
    UiList playlist_list_;

    UiLabel target_heading_, target_meta_;
    UiButton use_as_reference_, open_spotify_;
    UiListModel target_track_model_;
    UiList target_track_list_;

    UiLabel reference_heading_, reference_meta_;
    UiListModel reference_model_;
    UiList track_list_;

    UiLabel placement_heading_;
    UiDropdown order_mode_;
    UiLabel selection_heading_, selection_title_, selection_artist_, selection_state_;
    UiButton resolve_selected_, review_candidate_;
    UiLabel publish_heading_, preview_state_, notice_;
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
