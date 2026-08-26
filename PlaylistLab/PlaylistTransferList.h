#ifndef _PlaylistLab_PlaylistTransferList_h_
#define _PlaylistLab_PlaylistTransferList_h_

#include <Ui/Ui.h>

namespace Upp {

// PlaylistLab-specific copy drag between source lists and the Working list.
// UiList remains authoritative for selection and for Working's internal reorder;
// this adapter only transports selected row indices between distinct lists and
// surfaces semantic requests back to PlaylistWorkspace for document mutation.
class PlaylistTransferList : public UiList {
public:
    typedef PlaylistTransferList CLASSNAME;

    PlaylistTransferList& EnableTransferSource(bool on = true)
    {
        transfer_source_ = on;
        return *this;
    }

    PlaylistTransferList& EnableTransferTarget(bool on = true)
    {
        transfer_target_ = on;
        return *this;
    }

    PlaylistTransferList& EnableDeleteRequest(bool on = true)
    {
        delete_request_enabled_ = on;
        return *this;
    }

    const Vector<int>& GetTransferRows() const { return transfer_rows_; }

    Event<const PlaylistTransferList&, const Vector<int>&> WhenTransferDrop;
    Event<> WhenDeleteRequest;

    virtual void LeftDown(Point p, dword flags) override
    {
        Vector<int> prior_selection = GetSelection();
        Value prior_data = GetData();
        UiList::LeftDown(p, flags);

        transfer_pressed_ = transfer_source_ && !IsDragReorderEnabled() && GetCursor() >= 0;
        if(transfer_pressed_ && (flags & (K_SHIFT | K_CTRL)) == 0
           && prior_selection.GetCount() > 1
           && FindIndex(prior_selection, GetCursor()) >= 0) {
            // UiList intentionally makes an unmodified body click single-select.
            // For a drag beginning on an already-selected row, restore the prior
            // token selection so the visible group and transfer payload agree.
            SetData(prior_data);
        }
        if(transfer_pressed_ && !HasCapture())
            SetCapture();
    }

    virtual void LeftUp(Point p, dword flags) override
    {
        // Only release capture created by the transfer-source path. Working's
        // internal UiList reorder owns its own capture and must receive LeftUp.
        if(transfer_pressed_ && HasCapture())
            ReleaseCapture();
        transfer_pressed_ = false;
        UiList::LeftUp(p, flags);
    }

    virtual void LeftDrag(Point p, dword flags) override
    {
        if(!transfer_source_ || IsDragReorderEnabled()) {
            UiList::LeftDrag(p, flags);
            return;
        }
        if(!transfer_pressed_)
            return;

        transfer_rows_ = GetSelection();
        Sort(transfer_rows_);
        if(transfer_rows_.IsEmpty() && GetCursor() >= 0)
            transfer_rows_.Add(GetCursor());
        if(transfer_rows_.IsEmpty())
            return;

        transfer_pressed_ = false;
        if(HasCapture())
            ReleaseCapture();
        DoDragAndDrop(InternalClip(*this, "playlistlab-track-transfer"), Image(), DND_COPY);
    }

    virtual void DragAndDrop(Point, PasteClip& clip) override
    {
        if(!transfer_target_ || !IsAvailableInternal<PlaylistTransferList>(clip, "playlistlab-track-transfer")) {
            clip.Reject();
            return;
        }
        AcceptInternal<PlaylistTransferList>(clip, "playlistlab-track-transfer");
        clip.SetAction(DND_COPY);
        if(!clip.IsPaste())
            return;

        const PlaylistTransferList *source =
            GetInternalPtr<PlaylistTransferList>(clip, "playlistlab-track-transfer");
        if(source && source != this && WhenTransferDrop)
            WhenTransferDrop(*source, source->GetTransferRows());
    }

    virtual bool Key(dword key, int count) override
    {
        if(delete_request_enabled_ && key == K_DELETE && GetSelectionCount() > 0) {
            if(WhenDeleteRequest)
                WhenDeleteRequest();
            return true;
        }
        return UiList::Key(key, count);
    }

private:
    bool transfer_source_ = false;
    bool transfer_target_ = false;
    bool delete_request_enabled_ = false;
    bool transfer_pressed_ = false;
    Vector<int> transfer_rows_;
};

} // namespace Upp

#endif
