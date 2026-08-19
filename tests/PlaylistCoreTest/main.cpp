#include <PlaylistLab/PlaylistIO.h>
#include <PlaylistLab/PlaylistPlanner.h>

using namespace Upp;

static int checks;
static bool failed;

#define PL_CHECK(x) do { checks++; if(!(x)) { Cerr() << "FAIL line " << __LINE__ << ": " #x "\n"; failed = true; } } while(0)

CONSOLE_APP_MAIN
{
    {
        String csv = "title,artist,album,isrc,spotify_uri\n\"Long, Cool Song\",Artist,Album,,spotify:track:a\nSecond,Other,,,spotify:track:b\n";
        PlaylistImportResult r = ImportPlaylistCsv(csv, "set.csv");
        PL_CHECK(r.document.tracks.GetCount() == 2);
        PL_CHECK(r.document.tracks[0].requested_title == "Long, Cool Song");
        PL_CHECK(r.document.tracks[0].ResolvedUri() == "spotify:track:a");
        PL_CHECK(r.document.tracks[0].state == TRACK_EXACT);
        PL_CHECK(r.document.GetResolvedCount() == 2);
    }

    {
        PlaylistImportResult r = ImportPlaylistText("First Song - Artist One\nSecond Song\nThird Song\tArtist Three\n");
        PL_CHECK(r.document.tracks.GetCount() == 3);
        PL_CHECK(r.document.tracks[0].requested_title == "First Song");
        PL_CHECK(r.document.tracks[0].requested_artist == "Artist One");
        PL_CHECK(r.document.tracks[1].requested_artist.IsEmpty());
        PL_CHECK(r.document.tracks[2].requested_artist == "Artist Three");
    }

    {
        PlaylistDocument document;
        for(const char *title : { "A", "B", "C", "D" }) {
            TrackEntry entry;
            entry.requested_title = title;
            document.tracks.Add(pick(entry));
        }
        PL_CHECK(document.MoveTrack(1, 4));
        PL_CHECK(document.tracks[0].requested_title == "A");
        PL_CHECK(document.tracks[1].requested_title == "C");
        PL_CHECK(document.tracks[2].requested_title == "D");
        PL_CHECK(document.tracks[3].requested_title == "B");
        PL_CHECK(document.dirty);
        PL_CHECK(document.MoveTrack(3, 0));
        PL_CHECK(document.tracks[0].requested_title == "B");
        PL_CHECK(!document.MoveTrack(0, 1));
        PL_CHECK(!document.MoveTrack(-1, 0));
    }

    {
        Vector<String> reference = { "c", "a", "d" };
        Vector<String> target = { "a", "x", "b", "c", "y", "d" };
        PlaylistPlan p = BuildPlaylistPlan(reference, target, ORDER_REFERENCE_SLOTS);
        Vector<String> expected = { "c", "x", "b", "a", "y", "d" };
        PL_CHECK(p.desired_uris == expected);
        PL_CHECK(ApplyMoveSequence(clone(target), p.moves) == expected);
        PL_CHECK(p.matched_reference_count == 3);
        PL_CHECK(p.missing_reference_uris.IsEmpty());
    }

    {
        Vector<String> reference = { "c", "a", "missing" };
        Vector<String> target = { "a", "x", "b", "c", "y" };
        PlaylistPlan p = BuildPlaylistPlan(reference, target, ORDER_REFERENCE_FIRST);
        Vector<String> expected = { "c", "a", "x", "b", "y" };
        PL_CHECK(p.desired_uris == expected);
        PL_CHECK(ApplyMoveSequence(clone(target), p.moves) == expected);
        PL_CHECK(p.missing_reference_uris.GetCount() == 1);
        PL_CHECK(p.missing_reference_uris[0] == "missing");
    }

    {
        Vector<String> reference = { "a", "a", "b" };
        Vector<String> target = { "a", "b", "a", "z" };
        PlaylistPlan p = BuildPlaylistPlan(reference, target, ORDER_REFERENCE_FIRST);
        Vector<String> expected = { "a", "a", "b", "z" };
        PL_CHECK(p.desired_uris == expected);
        PL_CHECK(ApplyMoveSequence(clone(target), p.moves) == expected);
    }

    {
        Vector<String> reference = { "c", "a" };
        Vector<String> target = { "a", "playlistlab:unavailable:1", "x", "c" };
        PlaylistPlan p = BuildPlaylistPlan(reference, target, ORDER_REFERENCE_SLOTS);
        Vector<String> expected = { "c", "playlistlab:unavailable:1", "x", "a" };
        PL_CHECK(p.desired_uris == expected);
        PL_CHECK(ApplyMoveSequence(clone(target), p.moves) == expected);
        PL_CHECK(p.desired_uris[1] == "playlistlab:unavailable:1");
    }

    {
        TrackEntry request;
        request.requested_title = "Green Door";
        request.requested_artist = "Shakin' Stevens";
        SpotifyTrack candidate;
        candidate.title = "Green Door";
        candidate.artist = "Shakin' Stevens";
        PL_CHECK(ScoreTrackCandidate(request, candidate) >= 90);
        candidate.artist = "Different Artist";
        PL_CHECK(ScoreTrackCandidate(request, candidate) < 90);
    }

    {
        TrackEntry review;
        review.requested_title = "Is This Love";
        SpotifyTrack& candidate = review.candidates.Add();
        candidate.uri = "spotify:track:unconfirmed";
        candidate.title = "Is This Love";
        candidate.artist = "Candidate Artist";
        review.selected_candidate = 0;
        review.state = TRACK_REVIEW;
        review.confidence = 65;
        PL_CHECK(!review.IsResolved());
        PL_CHECK(review.ResolvedUri().IsEmpty());
        PL_CHECK(review.ResolvedTitle() == "Is This Love");

        review.SelectCandidate(0, TRACK_REVIEW);
        PL_CHECK(review.state == TRACK_REVIEW);
        PL_CHECK(!review.IsResolved());
        PL_CHECK(review.ResolvedUri().IsEmpty());

        review.SelectCandidate(0, TRACK_EXACT);
        PL_CHECK(review.IsResolved());
        PL_CHECK(review.ResolvedUri() == "spotify:track:unconfirmed");

        review.SelectCandidate(0, TRACK_MISSING);
        PL_CHECK(review.state == TRACK_UNRESOLVED);
        PL_CHECK(!review.IsResolved());
    }

    {
        SpotifyTrack episode;
        episode.type = "episode";
        episode.uri = "spotify:episode:e";
        PL_CHECK(episode.IsValid());
        PL_CHECK(!episode.placeholder);
    }

    {
        PlaylistDocument document;

        TrackEntry a;
        a.requested_title = "A";
        a.spotify_uri = "spotify:track:a";
        a.state = TRACK_EXACT;
        document.tracks.Add(pick(a));

        TrackEntry c;
        c.requested_title = "C";
        c.spotify_uri = "spotify:track:c";
        c.state = TRACK_AUTO;
        document.tracks.Add(pick(c));

        Vector<String> target = { "spotify:track:c", "spotify:track:x" };
        PlaylistPublishPreview preview = BuildPlaylistPublishPreview(document, target, ORDER_REFERENCE_FIRST);
        Vector<String> expected_add = { "spotify:track:a" };
        Vector<String> expected_final = { "spotify:track:a", "spotify:track:c", "spotify:track:x" };

        PL_CHECK(preview.CanPublish());
        PL_CHECK(preview.GetBlockingCount() == 0);
        PL_CHECK(preview.reference_count == 2);
        PL_CHECK(preview.publishable_count == 2);
        PL_CHECK(preview.add_uris == expected_add);
        PL_CHECK(preview.reorder_plan.desired_uris == expected_final);

        Vector<String> augmented = clone(target);
        for(const String& uri : preview.add_uris)
            augmented.Add(uri);
        PL_CHECK(ApplyMoveSequence(pick(augmented), preview.reorder_plan.moves) == expected_final);
    }

    {
        PlaylistDocument document;
        for(const char *uri : { "spotify:track:a", "spotify:track:a", "spotify:track:b" }) {
            TrackEntry entry;
            entry.spotify_uri = uri;
            entry.state = TRACK_EXACT;
            document.tracks.Add(pick(entry));
        }

        Vector<String> target = { "spotify:track:a", "spotify:track:z", "spotify:track:b" };
        PlaylistPublishPreview preview = BuildPlaylistPublishPreview(document, target, ORDER_REFERENCE_FIRST);
        Vector<String> expected_add = { "spotify:track:a" };
        Vector<String> expected_final = {
            "spotify:track:a", "spotify:track:a", "spotify:track:b", "spotify:track:z"
        };

        PL_CHECK(preview.CanPublish());
        PL_CHECK(preview.add_uris == expected_add);
        PL_CHECK(preview.reorder_plan.desired_uris == expected_final);
    }

    {
        PlaylistDocument document;

        TrackEntry exact;
        exact.spotify_uri = "spotify:track:ok";
        exact.state = TRACK_EXACT;
        document.tracks.Add(pick(exact));

        TrackEntry review;
        SpotifyTrack& candidate = review.candidates.Add();
        candidate.uri = "spotify:track:review";
        review.selected_candidate = 0;
        review.state = TRACK_REVIEW;
        document.tracks.Add(pick(review));

        TrackEntry missing;
        missing.state = TRACK_MISSING;
        document.tracks.Add(pick(missing));

        TrackEntry unresolved;
        unresolved.state = TRACK_UNRESOLVED;
        document.tracks.Add(pick(unresolved));

        PlaylistPublishPreview preview = BuildPlaylistPublishPreview(document, Vector<String>(), ORDER_REFERENCE_FIRST);
        PL_CHECK(!preview.CanPublish());
        PL_CHECK(preview.reference_count == 4);
        PL_CHECK(preview.publishable_count == 1);
        PL_CHECK(preview.GetBlockingCount() == 3);
        PL_CHECK(preview.review_count == 1);
        PL_CHECK(preview.missing_count == 1);
        PL_CHECK(preview.unresolved_count == 1);
        PL_CHECK(preview.reference_uris.GetCount() == 1);
        PL_CHECK(preview.reference_uris[0] == "spotify:track:ok");
    }

    {
        PlaylistDocument document;
        TrackEntry invalid;
        invalid.spotify_uri = "playlistlab:unavailable:0";
        invalid.state = TRACK_EXACT;
        document.tracks.Add(pick(invalid));

        PlaylistPublishPreview preview = BuildPlaylistPublishPreview(document, Vector<String>(), ORDER_REFERENCE_SLOTS);
        PL_CHECK(!preview.CanPublish());
        PL_CHECK(preview.publishable_count == 0);
        PL_CHECK(preview.invalid_uri_count == 1);
        PL_CHECK(preview.GetBlockingCount() == 1);
        PL_CHECK(preview.add_uris.IsEmpty());
    }

    {
        PlaylistDocument document;
        for(const char *uri : { "spotify:track:c", "spotify:track:a" }) {
            TrackEntry entry;
            entry.spotify_uri = uri;
            entry.state = TRACK_EXACT;
            document.tracks.Add(pick(entry));
        }

        Vector<String> target = {
            "spotify:track:a",
            "playlistlab:unavailable:1",
            "spotify:track:x",
            "spotify:track:c"
        };
        PlaylistPublishPreview preview = BuildPlaylistPublishPreview(document, target, ORDER_REFERENCE_SLOTS);
        Vector<String> expected = {
            "spotify:track:c",
            "playlistlab:unavailable:1",
            "spotify:track:x",
            "spotify:track:a"
        };
        PL_CHECK(preview.CanPublish());
        PL_CHECK(preview.add_uris.IsEmpty());
        PL_CHECK(preview.reorder_plan.desired_uris == expected);
        PL_CHECK(preview.reorder_plan.desired_uris[1] == "playlistlab:unavailable:1");
    }

    Cout() << checks << " checks completed\n";
    if(failed)
        SetExitCode(1);
}
