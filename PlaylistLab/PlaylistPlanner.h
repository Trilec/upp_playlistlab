#ifndef _PlaylistLab_PlaylistPlanner_h_
#define _PlaylistLab_PlaylistPlanner_h_

#include "PlaylistModel.h"

namespace Upp {

enum PlaylistOrderMode {
    ORDER_REFERENCE_SLOTS,
    ORDER_REFERENCE_FIRST,
};

struct PlaylistMove : Moveable<PlaylistMove> {
    int from = 0;
    int before = 0;
    int count = 1;
};

struct PlaylistPlan {
    Vector<String>       original_uris;
    Vector<String>       desired_uris;
    Vector<PlaylistMove> moves;
    Vector<String>       missing_reference_uris;
    int                  matched_reference_count = 0;

    bool IsNoOp() const { return moves.IsEmpty(); }
};

struct PlaylistPublishPreview {
    Vector<String> original_target_uris;
    Vector<String> reference_uris;
    Vector<String> add_uris;
    PlaylistPlan   reorder_plan;
    PlaylistOrderMode mode = ORDER_REFERENCE_SLOTS;

    int reference_count = 0;
    int publishable_count = 0;
    int review_count = 0;
    int missing_count = 0;
    int unresolved_count = 0;
    int invalid_uri_count = 0;

    int  GetBlockingCount() const { return max(0, reference_count - publishable_count); }
    bool CanPublish() const        { return reference_count > 0 && GetBlockingCount() == 0; }
    bool IsNoOp() const            { return add_uris.IsEmpty() && reorder_plan.IsNoOp(); }
};

PlaylistPlan BuildPlaylistPlan(const Vector<String>& reference_uris,
                               const Vector<String>& target_uris,
                               PlaylistOrderMode mode);
PlaylistPublishPreview BuildPlaylistPublishPreview(const PlaylistDocument& document,
                                                   const Vector<String>& target_uris,
                                                   PlaylistOrderMode mode);
bool ValidatePlaylistPublishPreview(const PlaylistPublishPreview& preview,
                                    const Vector<String>& current_target_uris,
                                    String *error = nullptr);
Vector<PlaylistMove> BuildMoveSequence(const Vector<String>& current_uris,
                                       const Vector<String>& desired_uris);
Vector<String> ApplyMoveSequence(Vector<String> current, const Vector<PlaylistMove>& moves);

} // namespace Upp

#endif
