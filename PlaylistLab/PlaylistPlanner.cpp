#include "PlaylistPlanner.h"

namespace Upp {
namespace {

String OccurrenceKey(const String& uri, int occurrence)
{
    return uri + "\n#" + AsString(occurrence);
}

Vector<String> MakeOccurrenceKeys(const Vector<String>& uris)
{
    VectorMap<String, int> seen;
    Vector<String> keys;
    for(const String& uri : uris) {
        int q = seen.Find(uri);
        int occurrence = 0;
        if(q < 0)
            seen.Add(uri, 1);
        else {
            occurrence = seen[q];
            seen[q]++;
        }
        keys.Add(OccurrenceKey(uri, occurrence));
    }
    return keys;
}

void MoveOne(Vector<String>& values, int from, int before)
{
    if(from < 0 || from >= values.GetCount() || before < 0 || before > values.GetCount() || from == before)
        return;
    String value = pick(values[from]);
    values.Remove(from);
    if(from < before)
        before--;
    values.Insert(before, pick(value));
}

bool IsSpotifyPlaylistItemUri(const String& uri)
{
    return uri.StartsWith("spotify:track:") || uri.StartsWith("spotify:episode:");
}

bool SameMoves(const Vector<PlaylistMove>& a, const Vector<PlaylistMove>& b)
{
    if(a.GetCount() != b.GetCount())
        return false;
    for(int i = 0; i < a.GetCount(); i++)
        if(a[i].from != b[i].from || a[i].before != b[i].before || a[i].count != b[i].count)
            return false;
    return true;
}

bool ValidationError(String *error, const String& text)
{
    if(error)
        *error = text;
    return false;
}

} // namespace

Vector<PlaylistMove> BuildMoveSequence(const Vector<String>& current_uris,
                                       const Vector<String>& desired_uris)
{
    Vector<PlaylistMove> moves;
    if(current_uris.GetCount() != desired_uris.GetCount())
        return moves;

    Vector<String> work = clone(current_uris);
    Vector<String> desired_keys = MakeOccurrenceKeys(desired_uris);

    for(int i = 0; i < desired_uris.GetCount(); i++) {
        if(work[i] == desired_uris[i])
            continue;

        Vector<String> work_keys = MakeOccurrenceKeys(work);
        int from = -1;
        for(int j = i + 1; j < work_keys.GetCount(); j++) {
            if(work_keys[j] == desired_keys[i]) {
                from = j;
                break;
            }
        }
        if(from < 0) {
            for(int j = i + 1; j < work.GetCount(); j++)
                if(work[j] == desired_uris[i]) {
                    from = j;
                    break;
                }
        }
        if(from < 0)
            return Vector<PlaylistMove>();

        PlaylistMove& move = moves.Add();
        move.from = from;
        move.before = i;
        move.count = 1;
        MoveOne(work, from, i);
    }
    return moves;
}

Vector<String> ApplyMoveSequence(Vector<String> current, const Vector<PlaylistMove>& moves)
{
    for(const PlaylistMove& move : moves)
        MoveOne(current, move.from, move.before);
    return current;
}

Vector<String> BuildAppendMissingUris(const Vector<String>& working_uris,
                                      const Vector<String>& target_uris)
{
    VectorMap<String, int> available;
    for(const String& uri : target_uris) {
        int q = available.Find(uri);
        if(q < 0)
            available.Add(uri, 1);
        else
            available[q]++;
    }

    VectorMap<String, int> consumed;
    Vector<String> missing;
    for(const String& uri : working_uris) {
        int used_q = consumed.Find(uri);
        int used = used_q < 0 ? 0 : consumed[used_q];
        int have_q = available.Find(uri);
        int have = have_q < 0 ? 0 : available[have_q];
        if(used >= have)
            missing.Add(uri);
        if(used_q < 0)
            consumed.Add(uri, 1);
        else
            consumed[used_q]++;
    }
    return missing;
}

PlaylistPlan BuildPlaylistPlan(const Vector<String>& reference_uris,
                               const Vector<String>& target_uris,
                               PlaylistOrderMode mode)
{
    PlaylistPlan plan;
    plan.original_uris = clone(target_uris);
    plan.desired_uris = clone(target_uris);

    Vector<String> reference_keys = MakeOccurrenceKeys(reference_uris);
    Vector<String> target_keys = MakeOccurrenceKeys(target_uris);
    Index<String> target_key_index;
    for(const String& key : target_keys)
        target_key_index.Add(key);

    Vector<int> matched_reference;
    Index<int> consumed_target;
    for(int i = 0; i < reference_keys.GetCount(); i++) {
        int q = target_key_index.Find(reference_keys[i]);
        if(q >= 0) {
            matched_reference.Add(i);
            consumed_target.Add(q);
            plan.matched_reference_count++;
        }
        else
            plan.missing_reference_uris.Add(reference_uris[i]);
    }

    if(mode == ORDER_REFERENCE_FIRST) {
        plan.desired_uris.Clear();
        for(int ref_index : matched_reference)
            plan.desired_uris.Add(reference_uris[ref_index]);
        for(int i = 0; i < target_uris.GetCount(); i++)
            if(consumed_target.Find(i) < 0)
                plan.desired_uris.Add(target_uris[i]);
    }
    else {
        Vector<int> slots;
        for(int i = 0; i < target_keys.GetCount(); i++)
            if(consumed_target.Find(i) >= 0)
                slots.Add(i);
        for(int i = 0; i < slots.GetCount() && i < matched_reference.GetCount(); i++)
            plan.desired_uris[slots[i]] = reference_uris[matched_reference[i]];
    }

    plan.moves = BuildMoveSequence(plan.original_uris, plan.desired_uris);
    return plan;
}

PlaylistPublishPreview BuildPlaylistPublishPreview(const PlaylistDocument& document,
                                                   const Vector<String>& target_uris,
                                                   PlaylistOrderMode mode)
{
    PlaylistPublishPreview preview;
    preview.original_target_uris = clone(target_uris);
    preview.mode = mode;
    preview.reference_count = document.tracks.GetCount();

    for(const TrackEntry& entry : document.tracks) {
        switch(entry.state) {
        case TRACK_REVIEW:     preview.review_count++; break;
        case TRACK_MISSING:    preview.missing_count++; break;
        case TRACK_UNRESOLVED: preview.unresolved_count++; break;
        default: break;
        }

        String uri = entry.ResolvedUri();
        if(uri.IsEmpty())
            continue;
        if(!IsSpotifyPlaylistItemUri(uri)) {
            preview.invalid_uri_count++;
            continue;
        }

        preview.reference_uris.Add(uri);
        preview.publishable_count++;
    }

    PlaylistPlan initial = BuildPlaylistPlan(preview.reference_uris, target_uris, mode);
    preview.add_uris = clone(initial.missing_reference_uris);

    Vector<String> augmented_target = clone(target_uris);
    for(const String& uri : preview.add_uris)
        augmented_target.Add(uri);

    preview.reorder_plan = BuildPlaylistPlan(preview.reference_uris, augmented_target, mode);
    return preview;
}

bool ValidatePlaylistPublishPreview(const PlaylistPublishPreview& preview,
                                    const Vector<String>& current_target_uris,
                                    String *error)
{
    if(error)
        error->Clear();
    if(!preview.CanPublish())
        return ValidationError(error, "Publish preview is blocked by unresolved reference rows.");
    if(preview.publishable_count != preview.reference_uris.GetCount() ||
       preview.reference_count != preview.publishable_count)
        return ValidationError(error, "Publish preview reference counts are inconsistent.");
    if(preview.original_target_uris != current_target_uris)
        return ValidationError(error, "Spotify target changed after this preview was created.");

    for(const String& uri : preview.add_uris)
        if(!IsSpotifyPlaylistItemUri(uri))
            return ValidationError(error, "Publish preview contains a non-publishable addition URI.");

    PlaylistPlan initial = BuildPlaylistPlan(preview.reference_uris, current_target_uris, preview.mode);
    if(initial.missing_reference_uris != preview.add_uris)
        return ValidationError(error, "Publish preview additions no longer match the deterministic plan.");

    Vector<String> augmented_target = clone(current_target_uris);
    for(const String& uri : preview.add_uris)
        augmented_target.Add(uri);
    if(preview.reorder_plan.original_uris != augmented_target)
        return ValidationError(error, "Publish preview reorder input does not match its planned additions.");

    PlaylistPlan expected = BuildPlaylistPlan(preview.reference_uris, augmented_target, preview.mode);
    if(expected.desired_uris != preview.reorder_plan.desired_uris ||
       !SameMoves(expected.moves, preview.reorder_plan.moves))
        return ValidationError(error, "Publish preview reorder plan is not deterministic for the current target.");

    if(ApplyMoveSequence(clone(augmented_target), preview.reorder_plan.moves) != preview.reorder_plan.desired_uris)
        return ValidationError(error, "Publish preview move sequence does not produce its desired target.");
    return true;
}

} // namespace Upp
