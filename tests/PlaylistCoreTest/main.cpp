#include <PlaylistWorkbench/PlaylistIO.h>
#include <PlaylistWorkbench/PlaylistPlanner.h>

using namespace Upp;

static int checks;
static bool failed;

#define CHECK(x) do { checks++; if(!(x)) { Cerr() << "FAIL line " << __LINE__ << ": " #x "\n"; failed = true; } } while(0)

CONSOLE_APP_MAIN
{
    {
        String csv = "title,artist,album,isrc,spotify_uri\n\"Long, Cool Song\",Artist,Album,,spotify:track:a\nSecond,Other,,,spotify:track:b\n";
        PlaylistImportResult r = ImportPlaylistCsv(csv, "set.csv");
        CHECK(r.document.tracks.GetCount() == 2);
        CHECK(r.document.tracks[0].requested_title == "Long, Cool Song");
        CHECK(r.document.tracks[0].ResolvedUri() == "spotify:track:a");
        CHECK(r.document.tracks[0].state == TRACK_EXACT);
        CHECK(r.document.GetResolvedCount() == 2);
    }

    {
        PlaylistImportResult r = ImportPlaylistText("First Song - Artist One\nSecond Song\nThird Song\tArtist Three\n");
        CHECK(r.document.tracks.GetCount() == 3);
        CHECK(r.document.tracks[0].requested_title == "First Song");
        CHECK(r.document.tracks[0].requested_artist == "Artist One");
        CHECK(r.document.tracks[1].requested_artist.IsEmpty());
        CHECK(r.document.tracks[2].requested_artist == "Artist Three");
    }

    {
        Vector<String> reference = { "c", "a", "d" };
        Vector<String> target = { "a", "x", "b", "c", "y", "d" };
        PlaylistPlan p = BuildPlaylistPlan(reference, target, ORDER_REFERENCE_SLOTS);
        Vector<String> expected = { "c", "x", "b", "a", "y", "d" };
        CHECK(p.desired_uris == expected);
        CHECK(ApplyMoveSequence(clone(target), p.moves) == expected);
        CHECK(p.matched_reference_count == 3);
        CHECK(p.missing_reference_uris.IsEmpty());
    }

    {
        Vector<String> reference = { "c", "a", "missing" };
        Vector<String> target = { "a", "x", "b", "c", "y" };
        PlaylistPlan p = BuildPlaylistPlan(reference, target, ORDER_REFERENCE_FIRST);
        Vector<String> expected = { "c", "a", "x", "b", "y" };
        CHECK(p.desired_uris == expected);
        CHECK(ApplyMoveSequence(clone(target), p.moves) == expected);
        CHECK(p.missing_reference_uris.GetCount() == 1);
        CHECK(p.missing_reference_uris[0] == "missing");
    }

    {
        Vector<String> reference = { "a", "a", "b" };
        Vector<String> target = { "a", "b", "a", "z" };
        PlaylistPlan p = BuildPlaylistPlan(reference, target, ORDER_REFERENCE_FIRST);
        Vector<String> expected = { "a", "a", "b", "z" };
        CHECK(p.desired_uris == expected);
        CHECK(ApplyMoveSequence(clone(target), p.moves) == expected);
    }

    {
        Vector<String> reference = { "c", "a" };
        Vector<String> target = { "a", "playlistlab:unavailable:1", "x", "c" };
        PlaylistPlan p = BuildPlaylistPlan(reference, target, ORDER_REFERENCE_SLOTS);
        Vector<String> expected = { "c", "playlistlab:unavailable:1", "x", "a" };
        CHECK(p.desired_uris == expected);
        CHECK(ApplyMoveSequence(clone(target), p.moves) == expected);
        CHECK(p.desired_uris[1] == "playlistlab:unavailable:1");
    }

    {
        TrackEntry request;
        request.requested_title = "Green Door";
        request.requested_artist = "Shakin' Stevens";
        SpotifyTrack candidate;
        candidate.title = "Green Door";
        candidate.artist = "Shakin' Stevens";
        CHECK(ScoreTrackCandidate(request, candidate) >= 90);
        candidate.artist = "Different Artist";
        CHECK(ScoreTrackCandidate(request, candidate) < 90);
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
        CHECK(!review.IsResolved());
        CHECK(review.ResolvedUri().IsEmpty());
        CHECK(review.ResolvedTitle() == "Is This Love");

        review.SelectCandidate(0, TRACK_EXACT);
        CHECK(review.IsResolved());
        CHECK(review.ResolvedUri() == "spotify:track:unconfirmed");
    }

    {
        SpotifyTrack episode;
        episode.type = "episode";
        episode.uri = "spotify:episode:e";
        CHECK(episode.IsValid());
        CHECK(!episode.placeholder);
    }

    Cout() << checks << " checks completed\n";
    if(failed)
        SetExitCode(1);
}
