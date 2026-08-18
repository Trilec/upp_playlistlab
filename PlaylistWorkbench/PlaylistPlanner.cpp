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

} // namespace Upp
