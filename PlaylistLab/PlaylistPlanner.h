#ifndef _PlaylistWorkbench_PlaylistPlanner_h_
#define _PlaylistWorkbench_PlaylistPlanner_h_

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

PlaylistPlan BuildPlaylistPlan(const Vector<String>& reference_uris,
                               const Vector<String>& target_uris,
                               PlaylistOrderMode mode);
Vector<PlaylistMove> BuildMoveSequence(const Vector<String>& current_uris,
                                       const Vector<String>& desired_uris);
Vector<String> ApplyMoveSequence(Vector<String> current, const Vector<PlaylistMove>& moves);

} // namespace Upp

#endif
