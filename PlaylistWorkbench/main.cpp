#ifdef flagMAIN

#include <CtrlLib/CtrlLib.h>
#include <Ui/Ui.h>
#include "PlaylistIO.h"

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

class PlaylistWorkbenchWindow : public TopWindow {
public:
    typedef PlaylistWorkbenchWindow CLASSNAME;

    PlaylistWorkbenchWindow()
    {
        Title("PlaylistLab");
        Sizeable().Zoomable();
        SetRect(0, 0, DPI(1180), DPI(760));

        BuildHeader();
        BuildBody();
        ConnectEvents();
        RefreshProjection();
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
            int rail_h = min(DPI(210), max(DPI(160), content_h / 3));
            int list_h = max(0, content_h - rail_h - gap);
            list_panel_.SetRect(margin, top, content_w, list_h);
            rail_panel_.SetRect(margin, top + list_h + gap, content_w, rail_h);
        }

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

        list_panel_.Add(track_list_.SizePos());
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
        rail_panel_.Add(notice_);

        document_heading_.SetText("DOCUMENT");
        selection_heading_.SetText("SELECTION");
    }

    void ConnectEvents()
    {
        import_csv_.WhenAction = [=] { ImportCsv(); };
        paste_text_.WhenAction = [=] { ImportClipboardText(); };
        export_csv_.WhenAction = [=] { ExportCsv(); };
        clear_.WhenAction = [=] { ClearDocument(); };
        track_list_.WhenSelection = [=] { UpdateSelection(); };
        track_list_.WhenReorderRequest = [=](UiReorderRequest& request) { ReorderDocument(request); };
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
        source_.SetRect(x, y, w, DPI(22)); y += DPI(34);

        selection_heading_.SetRect(x, y, w, DPI(20)); y += DPI(24);
        selection_title_.SetRect(x, y, w, DPI(24)); y += DPI(24);
        selection_artist_.SetRect(x, y, w, DPI(22)); y += DPI(22);
        selection_state_.SetRect(x, y, w, DPI(22)); y += DPI(22);
        selection_uri_.SetRect(x, y, w, DPI(40));

        int notice_h = DPI(42);
        notice_.SetRect(x, max(y + DPI(48), rc.GetHeight() - notice_h - DPI(12)), w, notice_h);
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
        int target = request.before;
        if(!document_.MoveTrack(request.from, request.before)) {
            request.accept = false;
            return;
        }

        request.handled = true;
        if(target > request.from)
            target--;
        last_notice_ = "Order changed locally. No Spotify write was made.";
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
        header_.SetSubTitle(subtitle);

        document_name_.SetText(document_.name.IsEmpty() ? "Untitled working list" : document_.name);
        document_counts_.SetText(Format("%d tracks  /  %d publishable", total, resolved));
        document_attention_.SetText(Format("%d review  /  %d missing", review, missing));
        source_.SetText(document_.source_path.IsEmpty() ? "Source: local / unsaved" : "Source: " + GetFileName(document_.source_path));
        notice_.SetText(last_notice_.IsEmpty() ? "Import, review, order, then preview before publishing." : last_notice_);

        export_csv_.Enable(total > 0);
        clear_.Enable(total > 0);
    }

    void UpdateSelection()
    {
        int index = track_list_.GetCursor();
        if(index < 0 || index >= document_.tracks.GetCount()) {
            selection_title_.SetText("No track selected");
            selection_artist_.SetText(Null);
            selection_state_.SetText(Null);
            selection_uri_.SetText(Null);
            return;
        }

        const TrackEntry& entry = document_.tracks[index];
        selection_title_.SetText(TrackDisplayTitle(entry));
        selection_artist_.SetText(entry.ResolvedArtist().IsEmpty() ? "Artist: -" : "Artist: " + entry.ResolvedArtist());
        selection_state_.SetText("State: " + TrackMatchStateText(entry.state));
        String uri = entry.ResolvedUri();
        selection_uri_.SetText(uri.IsEmpty() ? "Spotify URI: not publishable" : "Spotify URI: " + uri);
    }

private:
    PlaylistDocument document_;
    UiListModel row_model_;
    String last_notice_ = "Ready for CSV import or clipboard text.";

    UiTitleCard header_;
    UiBoxLayout header_actions_ { UiDirection::H };
    UiButton import_csv_, paste_text_, export_csv_, clear_;

    UiPanel list_panel_, rail_panel_;
    UiList track_list_;

    UiLabel document_heading_, document_name_, document_counts_, document_attention_, source_;
    UiLabel selection_heading_, selection_title_, selection_artist_, selection_state_, selection_uri_;
    UiLabel notice_;
};

} // namespace

GUI_APP_MAIN
{
    PlaylistWorkbenchWindow().Run();
}

#endif
