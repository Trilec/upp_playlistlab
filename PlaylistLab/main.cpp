#ifdef flagMAIN

#include <CtrlLib/CtrlLib.h>
#include <Ui/Ui.h>
#include "PlaylistIO.h"
#include "PlaylistPlanner.h"
#include "SpotifyClient.h"

using namespace Upp;

namespace {

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
            out << " | ";
        out << album;
    }
    if(out.IsEmpty() && !entry.ResolvedUri().IsEmpty())
        out = entry.ResolvedUri();
    return out;
}

String OrderModeText(PlaylistOrderMode mode)
{
    return mode == ORDER_REFERENCE_FIRST ? "Reference First" : "Reference Slots";
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
        SetRect(0, 0, DPI(680), DPI(480));

        Add(list_);
        Add(ok_);
        Add(cancel_);

        list_.SetModel(model_)
             .EnableRenameOnDblClick(false)
             .EnableDragReorder(false)
             .ShowDragHandle(false);
        model_.AddRange(rows);

        ok_.SetText("Select");
        cancel_.SetText("Cancel");
        ok_.WhenAction = [=] {
            if(list_.GetCursor() >= 0)
                AcceptBreak(IDOK);
        };
        cancel_.WhenAction = [=] { RejectBreak(IDCANCEL); };
    }

    virtual void Layout() override
    {
        Rect rc = GetSize();
        int margin = DPI(12);
        int gap = DPI(8);
        int button_h = DPI(34);
        int button_w = DPI(92);
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
        SetRect(0, 0, DPI(760), DPI(560));

        Add(target_);
        Add(summary_);
        Add(detail_);
        Add(list_);
        Add(publish_);
        Add(close_);

        target_.SetText(target);
        summary_.SetText(summary);
        detail_.SetText(detail);
        publish_.SetText("Publish to Spotify");
        publish_.Enable(can_publish);
        close_.SetText("Close");
        publish_.WhenAction = [=] { AcceptBreak(IDYES); };
        close_.WhenAction = [=] { RejectBreak(IDCANCEL); };

        list_.SetModel(model_)
             .EnableRenameOnDblClick(false)
             .EnableDragReorder(false)
             .ShowDragHandle(false);
        model_.AddRange(rows);

        UiList::Style style = UiList::StyleDefault();
        style.row_height = DPI(40);
        style.show_checks = false;
        style.show_icons = false;
        style.right_text_as_badge = true;
        list_.SetCustomStyle(style);
    }

    virtual void Layout() override
    {
        Rect rc = GetSize();
        int margin = DPI(14);
        int gap = DPI(6);
        int y = margin;
        int w = max(0, rc.GetWidth() - margin * 2);

        target_.SetRect(margin, y, w, DPI(24)); y += DPI(28);
        summary_.SetRect(margin, y, w, DPI(22)); y += DPI(26);
        detail_.SetRect(margin, y, w, DPI(42)); y += DPI(48);

        int button_h = DPI(34);
        int close_w = DPI(92);
        int publish_w = DPI(144);
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
        SetRect(0, 0, DPI(1180), DPI(760));

        BuildHeader();
        BuildBody();
        ConnectEvents();
        RefreshProjection();
    }

    ~PlaylistLabWindow()
    {
        if(spotify_worker_.IsOpen())
            spotify_worker_.Wait();
    }

    virtual void Close() override
    {
        if(spotify_busy_) {
            last_notice_ = "Finish the current Spotify operation before closing PlaylistLab.";
            UpdateSummary();
            return;
        }
        TopWindow::Close();
    }

    virtual void Layout() override
    {
        Rect rc = GetSize();
        const int margin = DPI(12);
        const int gap = DPI(10);
        const int header_h = DPI(78);

        header_.SetRect(margin, margin, max(0, rc.GetWidth() - margin * 2), header_h);

        int top = margin + header_h + gap;
        int content_h = max(0, rc.GetHeight() - top - margin);
        int content_w = max(0, rc.GetWidth() - margin * 2);

        if(content_w >= DPI(760)) {
            int rail_w = min(DPI(300), max(DPI(240), content_w / 4));
            int list_w = max(0, content_w - rail_w - gap);
            list_panel_.SetRect(margin, top, list_w, content_h);
            rail_panel_.SetRect(margin + list_w + gap, top, rail_w, content_h);
        }
        else {
            int wanted = min(DPI(340), max(DPI(280), content_h / 2));
            int rail_h = min(content_h, wanted);
            int list_h = max(0, content_h - rail_h - gap);
            list_panel_.SetRect(margin, top, content_w, list_h);
            rail_panel_.SetRect(margin, top + list_h + gap, content_w, rail_h);
        }

        LayoutListPanel();
        LayoutRail();
    }

private:
    void BuildHeader()
    {
        Add(header_);
        header_.SetTitle("PlaylistLab")
               .SetSubTitle("Local playlist document")
               .ShowTitleLine(false)
               .SetContentInset(DPI(8))
               .SetContentCell(header_actions_);

        header_actions_.SetGap(DPI(6)).SetInset(0).SetAlignItems(UiCrossAlign::Center);
        header_actions_.AddSpacer(1).Expand(1);

        import_csv_.SetText("Import CSV");
        paste_text_.SetText("Paste Text");
        export_csv_.SetText("Export CSV");
        clear_.SetText("Clear");

        header_actions_.Add(import_csv_).Fixed(DPI(96));
        header_actions_.Add(paste_text_).Fixed(DPI(96));
        header_actions_.Add(export_csv_).Fixed(DPI(96));
        header_actions_.Add(clear_).Fixed(DPI(72));
    }

    void BuildBody()
    {
        Add(list_panel_);
        Add(rail_panel_);

        list_panel_.Add(spotify_actions_);
        list_panel_.Add(track_list_);

        order_mode_.Add("Reference Slots", (int)ORDER_REFERENCE_SLOTS);
        order_mode_.Add("Reference First", (int)ORDER_REFERENCE_FIRST);
        order_mode_.Select(0);
        order_mode_.SetPopupMaxItems(2);

        spotify_target_.SetText("Spotify Target");
        resolve_selected_.SetText("Resolve Selected");
        review_candidate_.SetText("Review Candidate");
        preview_.SetText("Preview");

        spotify_actions_.SetGap(DPI(6)).SetInset(DPI(4)).SetAlignItems(UiCrossAlign::Center);
        spotify_actions_.Add(order_mode_).Fixed(DPI(136));
        spotify_actions_.AddSpacer(1).Expand(1);
        spotify_actions_.Add(spotify_target_).Fixed(DPI(112));
        spotify_actions_.Add(resolve_selected_).Fixed(DPI(124));
        spotify_actions_.Add(review_candidate_).Fixed(DPI(124));
        spotify_actions_.Add(preview_).Fixed(DPI(82));

        track_list_.SetModel(row_model_)
                   .EnableRenameOnDblClick(false)
                   .EnableDragReorder(true)
                   .EnableInternalMutation(false)
                   .ShowDragHandle(true)
                   .SetDragSide(UiAlign::RIGHT);

        UiList::Style list_style = UiList::StyleDefault();
        list_style.row_height = DPI(42);
        list_style.show_checks = false;
        list_style.show_icons = false;
        list_style.show_metadata_marker = true;
        list_style.right_text_as_badge = true;
        track_list_.SetCustomStyle(list_style);

        rail_panel_.Add(document_heading_);
        rail_panel_.Add(document_name_);
        rail_panel_.Add(document_counts_);
        rail_panel_.Add(document_attention_);
        rail_panel_.Add(source_);
        rail_panel_.Add(selection_heading_);
        rail_panel_.Add(selection_title_);
        rail_panel_.Add(selection_artist_);
        rail_panel_.Add(selection_state_);
        rail_panel_.Add(selection_uri_);
        rail_panel_.Add(spotify_heading_);
        rail_panel_.Add(target_);
        rail_panel_.Add(preview_state_);
        rail_panel_.Add(notice_);

        document_heading_.SetText("DOCUMENT");
        selection_heading_.SetText("SELECTION");
        spotify_heading_.SetText("SPOTIFY");
    }

    void ConnectEvents()
    {
        import_csv_.WhenAction = [=] { ImportCsv(); };
        paste_text_.WhenAction = [=] { ImportClipboardText(); };
        export_csv_.WhenAction = [=] { ExportCsv(); };
        clear_.WhenAction = [=] { ClearDocument(); };
        spotify_target_.WhenAction = [=] { StartLoadPlaylists(); };
        resolve_selected_.WhenAction = [=] { StartResolveSelected(); };
        review_candidate_.WhenAction = [=] { ReviewSelectedCandidate(); };
        preview_.WhenAction = [=] { ShowPreview(); };
        order_mode_.WhenSelect = [=](int) {
            order_mode_value_ = order_mode_.GetSelection() == 1 ? ORDER_REFERENCE_FIRST
                                                               : ORDER_REFERENCE_SLOTS;
            UpdateSummary();
        };
        track_list_.WhenSelection = [=] { UpdateSelection(); };
        track_list_.WhenReorderRequest = [=](UiReorderRequest& request) { ReorderDocument(request); };
    }

    void LayoutListPanel()
    {
        Rect rc = list_panel_.GetSize();
        int toolbar_h = DPI(42);
        int gap = DPI(4);
        spotify_actions_.SetRect(0, 0, rc.GetWidth(), min(toolbar_h, rc.GetHeight()));
        int top = min(rc.GetHeight(), toolbar_h + gap);
        track_list_.SetRect(0, top, rc.GetWidth(), max(0, rc.GetHeight() - top));
    }

    void LayoutRail()
    {
        Rect rc = rail_panel_.GetSize();
        int x = DPI(14);
        int y = DPI(12);
        int w = max(0, rc.GetWidth() - DPI(28));

        document_heading_.SetRect(x, y, w, DPI(20)); y += DPI(24);
        document_name_.SetRect(x, y, w, DPI(24)); y += DPI(26);
        document_counts_.SetRect(x, y, w, DPI(22)); y += DPI(22);
        document_attention_.SetRect(x, y, w, DPI(22)); y += DPI(22);
        source_.SetRect(x, y, w, DPI(22)); y += DPI(32);

        selection_heading_.SetRect(x, y, w, DPI(20)); y += DPI(24);
        selection_title_.SetRect(x, y, w, DPI(24)); y += DPI(24);
        selection_artist_.SetRect(x, y, w, DPI(22)); y += DPI(22);
        selection_state_.SetRect(x, y, w, DPI(22)); y += DPI(22);
        selection_uri_.SetRect(x, y, w, DPI(38)); y += DPI(46);

        spotify_heading_.SetRect(x, y, w, DPI(20)); y += DPI(24);
        target_.SetRect(x, y, w, DPI(24)); y += DPI(24);
        preview_state_.SetRect(x, y, w, DPI(40)); y += DPI(46);

        int notice_h = DPI(42);
        notice_.SetRect(x, max(y, rc.GetHeight() - notice_h - DPI(12)), w, notice_h);
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
        last_notice_ = warning_count
                     ? Format("Imported with %d warning%s.", warning_count, warning_count == 1 ? "" : "s")
                     : String("CSV imported. No remote changes were made.");
        RefreshProjection(document_.tracks.IsEmpty() ? -1 : 0);
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
        last_notice_ = warning_count
                     ? Format("Clipboard import produced %d warning%s.", warning_count, warning_count == 1 ? "" : "s")
                     : String("Clipboard list imported as a local working document.");
        RefreshProjection(document_.tracks.IsEmpty() ? -1 : 0);
    }

    void ExportCsv()
    {
        if(document_.tracks.IsEmpty()) {
            Exclamation("There is no playlist content to export.");
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
        last_notice_ = "CSV exported. Spotify was not modified.";
        UpdateSummary();
    }

    void ClearDocument()
    {
        if(document_.dirty && !PromptYesNo("Discard the current local playlist changes?"))
            return;
        document_.Clear();
        last_notice_ = "Ready for CSV import or clipboard text.";
        RefreshProjection();
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
        last_notice_ = "Order changed locally. Preview again before publishing.";
        RefreshProjection(target);
    }

    void RefreshProjection(int selected = -1)
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

        row_model_.Clear();
        if(!rows.IsEmpty())
            row_model_.AddRange(rows);

        UpdateSummary();
        if(selected >= 0 && selected < document_.tracks.GetCount())
            track_list_.SetCursor(selected);
        else
            UpdateSelection();
    }

    void UpdateSummary()
    {
        int total = document_.tracks.GetCount();
        int resolved = document_.GetResolvedCount();
        int review = document_.GetReviewCount();
        int missing = document_.GetMissingCount();

        String subtitle = Format("%d tracks | %d publishable | %d review | %d missing", total, resolved, review, missing);
        if(document_.dirty)
            subtitle << " | modified";
        if(spotify_busy_)
            subtitle << " | Spotify working";
        header_.SetSubTitle(subtitle);

        document_name_.SetText(document_.name.IsEmpty() ? "Untitled working list" : document_.name);
        document_counts_.SetText(Format("%d tracks  /  %d publishable", total, resolved));
        document_attention_.SetText(Format("%d review  /  %d missing", review, missing));
        source_.SetText(document_.source_path.IsEmpty() ? "Source: local / unsaved" : "Source: " + GetFileName(document_.source_path));

        target_.SetText(target_loaded_ ? "Target: " + target_playlist_name_ : "Target: not loaded");
        if(target_loaded_ && total > 0) {
            PlaylistPublishPreview p = BuildPlaylistPublishPreview(document_, target_uris_, order_mode_value_);
            preview_state_.SetText(Format("%s: %d add / %d move / %d blocked",
                                         OrderModeText(order_mode_value_), p.add_uris.GetCount(),
                                         p.reorder_plan.moves.GetCount(), p.GetBlockingCount()));
        }
        else
            preview_state_.SetText("Preview: load a target and reference list");

        notice_.SetText(last_notice_.IsEmpty() ? "Import, resolve, order, preview, then explicitly publish." : last_notice_);
        UpdateActionState();
    }

    void UpdateSelection()
    {
        int index = track_list_.GetCursor();
        if(index < 0 || index >= document_.tracks.GetCount()) {
            selection_title_.SetText("No track selected");
            selection_artist_.SetText(Null);
            selection_state_.SetText(Null);
            selection_uri_.SetText(Null);
            UpdateActionState();
            return;
        }

        const TrackEntry& entry = document_.tracks[index];
        selection_title_.SetText(TrackDisplayTitle(entry));
        selection_artist_.SetText(entry.ResolvedArtist().IsEmpty() ? "Artist: -" : "Artist: " + entry.ResolvedArtist());
        selection_state_.SetText("State: " + TrackMatchStateText(entry.state));
        String uri = entry.ResolvedUri();
        selection_uri_.SetText(uri.IsEmpty() ? "Spotify URI: not publishable" : "Spotify URI: " + uri);
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
        order_mode_.Enable(idle);
        spotify_target_.Enable(idle);
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

    void StartLoadPlaylists()
    {
        if(!EnsureSpotifyClientId() || !PrepareSpotifyWorker())
            return;

        SetSpotifyBusy(true, "Connecting to Spotify and loading editable playlists...");
        if(!spotify_worker_.Run([=] {
            Vector<SpotifyPlaylistInfo> found;
            String error;
            bool ok = EnsureSpotifyAuthorizedWorker(error);
            if(ok && !spotify_client_.GetEditablePlaylists(found)) {
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
        if(spotify_playlists_.IsEmpty()) {
            last_notice_ = "Spotify returned no editable playlists for this account.";
            UpdateSummary();
            return;
        }

        Vector<UiModelItem> rows;
        rows.Reserve(spotify_playlists_.GetCount());
        for(int i = 0; i < spotify_playlists_.GetCount(); i++) {
            const SpotifyPlaylistInfo& playlist = spotify_playlists_[i];
            UiModelItem& row = rows.Add();
            row.text = playlist.name.IsEmpty() ? String("Untitled Spotify playlist") : playlist.name;
            row.description = playlist.owner_name.IsEmpty() ? playlist.owner_id : playlist.owner_name;
            row.right_text = Format("%d items", playlist.item_count);
            row.data = i;
        }

        UiChoiceDialog dialog("Choose Spotify Target", rows);
        int selected = dialog.Choose();
        if(selected >= 0)
            StartLoadTarget(selected);
        else {
            last_notice_ = "Spotify target selection cancelled.";
            UpdateSummary();
        }
    }

    void StartLoadTarget(int playlist_index)
    {
        if(playlist_index < 0 || playlist_index >= spotify_playlists_.GetCount() || !PrepareSpotifyWorker())
            return;

        target_playlist_id_ = spotify_playlists_[playlist_index].id;
        target_playlist_name_ = spotify_playlists_[playlist_index].name;
        target_snapshot_id_.Clear();
        target_loaded_ = false;
        target_tracks_.Clear();
        target_uris_.Clear();

        String playlist_id = target_playlist_id_;
        SetSpotifyBusy(true, "Loading Spotify target playlist...");
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
            last_notice_ = error.IsEmpty() ? "Spotify target loading failed." : error;
            UpdateSummary();
            Exclamation(last_notice_);
            return;
        }

        target_uris_.Clear();
        target_uris_.Reserve(target_tracks_.GetCount());
        for(const SpotifyTrack& track : target_tracks_)
            target_uris_.Add(track.uri);
        target_loaded_ = true;
        last_notice_ = Format("Loaded Spotify target '%s' with %d item%s. Inspect Preview before any write.",
                              target_playlist_name_, target_uris_.GetCount(),
                              target_uris_.GetCount() == 1 ? "" : "s");
        UpdateSummary();
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
            last_notice_ = "The local track changed before Spotify resolution completed.";
            UpdateSummary();
            return;
        }

        Swap(document_.tracks[index], *pending_resolution_);
        pending_resolution_.Clear();
        document_.dirty = true;
        TrackMatchState state = document_.tracks[index].state;
        last_notice_ = state == TRACK_REVIEW
                     ? "Spotify candidates loaded. Confirm a candidate before it can be published."
                     : state == TRACK_MISSING
                       ? "Spotify did not return a usable candidate for this track."
                       : "Spotify resolution updated the local document. Preview before publishing.";
        RefreshProjection(index);

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
                    row.description << " | ";
                row.description << candidate.album;
            }
            row.right_text = Format("%d", ScoreTrackCandidate(entry, candidate));
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
        RefreshProjection(index);
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
                return text.IsEmpty() ? uri : text + " | " + uri;
            }
        for(const SpotifyTrack& track : target_tracks_)
            if(track.uri == uri) {
                String text = track.artist;
                if(!track.album.IsEmpty()) {
                    if(!text.IsEmpty())
                        text << " | ";
                    text << track.album;
                }
                return text.IsEmpty() ? uri : text + " | " + uri;
            }
        return uri;
    }

    void StartPublishPreview(PlaylistPublishPreview& preview)
    {
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
        }
        else if(result.partial) {
            target_loaded_ = false;
            target_tracks_.Clear();
            target_uris_.Clear();
            target_snapshot_id_.Clear();
        }
        pending_publish_preview_.Clear();

        if(ok && result.success) {
            last_notice_ = Format("Spotify publish verified: %d added / %d move%s. No items were deleted.",
                                  result.added_count, result.move_count, result.move_count == 1 ? "" : "s");
            UpdateSummary();
            return;
        }

        if(result.stale)
            last_notice_ = "Publish cancelled because the Spotify target changed. The observed target was refreshed; inspect a new preview.";
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
            Exclamation("Load a Spotify target playlist before previewing.");
            return;
        }
        if(document_.tracks.IsEmpty()) {
            Exclamation("Import a reference playlist before previewing.");
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

        String summary = Format("%d reference | %d publishable | %d blocked | %d add | %d move",
                                p.reference_count, p.publishable_count, p.GetBlockingCount(),
                                p.add_uris.GetCount(), p.reorder_plan.moves.GetCount());
        String detail;
        bool snapshot_ready = !target_snapshot_id_.IsEmpty();
        if(!p.CanPublish())
            detail = Format("BLOCKED — review %d, missing %d, unresolved %d, invalid URI %d. Resolve every reference row before publishing.",
                            p.review_count, p.missing_count, p.unresolved_count, p.invalid_uri_count);
        else if(!snapshot_ready)
            detail = "BLOCKED — the target has no snapshot evidence. Reload the Spotify target before publishing.";
        else if(p.IsNoOp())
            detail = "READY — target already matches this preview. No Spotify mutation is required.";
        else
            detail = "READY — Publish executes this exact preview after a fresh target/snapshot preflight, then verifies the final target.";

        bool can_publish = p.CanPublish() && snapshot_ready && !p.IsNoOp();
        PreviewDialog dialog("Target: " + target_playlist_name_ + " | " + OrderModeText(order_mode_value_),
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

        last_notice_ = p.CanPublish()
                     ? "Preview inspected. Spotify was not modified."
                     : "Preview is blocked until every reference row is publishable.";
        UpdateSummary();
    }

private:
    PlaylistDocument document_;
    UiListModel row_model_;
    String last_notice_ = "Ready for CSV import or clipboard text.";

    SpotifyAuth   spotify_auth_;
    SpotifyClient spotify_client_;
    Thread        spotify_worker_;
    bool          spotify_busy_ = false;
    bool          pending_spotify_ok_ = false;
    String        pending_spotify_error_;

    Vector<SpotifyPlaylistInfo> spotify_playlists_;
    Vector<SpotifyTrack> target_tracks_;
    Vector<String>       target_uris_;
    String target_playlist_id_;
    String target_playlist_name_;
    String target_snapshot_id_;
    bool   target_loaded_ = false;

    One<TrackEntry> pending_resolution_;
    int             pending_resolution_index_ = -1;
    One<PlaylistPublishPreview> pending_publish_preview_;
    SpotifyPublishResult        pending_publish_result_;
    PlaylistOrderMode order_mode_value_ = ORDER_REFERENCE_SLOTS;

    UiTitleCard header_;
    UiBoxLayout header_actions_ { UiDirection::H };
    UiButton import_csv_, paste_text_, export_csv_, clear_;

    UiPanel list_panel_, rail_panel_;
    UiBoxLayout spotify_actions_ { UiDirection::H };
    UiDropdown order_mode_;
    UiButton spotify_target_, resolve_selected_, review_candidate_, preview_;
    UiList track_list_;

    UiLabel document_heading_, document_name_, document_counts_, document_attention_, source_;
    UiLabel selection_heading_, selection_title_, selection_artist_, selection_state_, selection_uri_;
    UiLabel spotify_heading_, target_, preview_state_;
    UiLabel notice_;
};

} // namespace

GUI_APP_MAIN
{
    PlaylistLabWindow().Run();
}

#endif
