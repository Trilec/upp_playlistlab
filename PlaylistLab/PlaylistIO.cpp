#include "PlaylistIO.h"

namespace Upp {
namespace {

String CsvQuote(const String& value)
{
    if(value.Find(',') < 0 && value.Find('"') < 0 && value.Find('\n') < 0 && value.Find('\r') < 0)
        return value;
    String out = "\"";
    for(int i = 0; i < value.GetCount(); i++) {
        if(value[i] == '"')
            out << "\"\"";
        else
            out.Cat(value[i]);
    }
    return out << '"';
}

Vector< Vector<String> > ParseDelimited(const String& text, char delimiter, Vector<String>& warnings)
{
    Vector< Vector<String> > rows;
    Vector<String> row;
    String field;
    bool quoted = false;

    for(int i = 0; i <= text.GetCount(); i++) {
        int c = i < text.GetCount() ? (byte)text[i] : '\n';
        if(quoted) {
            if(c == '"') {
                if(i + 1 < text.GetCount() && text[i + 1] == '"') {
                    field.Cat('"');
                    i++;
                }
                else
                    quoted = false;
            }
            else
                field.Cat(c);
            continue;
        }

        if(c == '"' && field.IsEmpty()) {
            quoted = true;
            continue;
        }
        if(c == delimiter) {
            row.Add(field);
            field.Clear();
            continue;
        }
        if(c == '\r')
            continue;
        if(c == '\n') {
            row.Add(field);
            field.Clear();
            bool nonempty = false;
            for(const String& s : row)
                nonempty = nonempty || !TrimBoth(s).IsEmpty();
            if(nonempty)
                rows.Add(pick(row));
            row.Clear();
            continue;
        }
        field.Cat(c);
    }

    if(quoted)
        warnings.Add("CSV ended inside a quoted field; imported available text.");
    return rows;
}

char DetectDelimiter(const String& first_line)
{
    int commas = 0, tabs = 0, semicolons = 0;
    bool quoted = false;
    for(int i = 0; i < first_line.GetCount(); i++) {
        int c = first_line[i];
        if(c == '"') quoted = !quoted;
        else if(!quoted && c == ',') commas++;
        else if(!quoted && c == '\t') tabs++;
        else if(!quoted && c == ';') semicolons++;
    }
    if(tabs > commas && tabs >= semicolons) return '\t';
    if(semicolons > commas) return ';';
    return ',';
}

String HeaderKey(const String& value)
{
    String key = NormalizeTrackText(value);
    key.Replace(" ", "_");
    return key;
}

int FindColumn(const Vector<String>& header, const Vector<String>& aliases)
{
    for(int i = 0; i < header.GetCount(); i++) {
        String key = HeaderKey(header[i]);
        for(const String& alias : aliases)
            if(key == alias)
                return i;
    }
    return -1;
}

String Cell(const Vector<String>& row, int column)
{
    return column >= 0 && column < row.GetCount() ? TrimBoth(row[column]) : String();
}

bool LooksLikeHeader(const Vector<String>& row)
{
    for(const String& value : row) {
        String key = HeaderKey(value);
        if(key == "title" || key == "track" || key == "song" || key == "artist" ||
           key == "spotify_uri" || key == "uri" || key == "isrc")
            return true;
    }
    return false;
}

TrackEntry EntryFromCells(const Vector<String>& row, int title_col, int artist_col,
                          int album_col, int isrc_col, int uri_col)
{
    TrackEntry entry;
    entry.requested_title = Cell(row, title_col);
    entry.requested_artist = Cell(row, artist_col);
    entry.requested_album = Cell(row, album_col);
    entry.requested_isrc = Cell(row, isrc_col);
    entry.spotify_uri = Cell(row, uri_col);
    if(!entry.spotify_uri.IsEmpty()) {
        entry.state = TRACK_EXACT;
        entry.confidence = 100;
    }
    return entry;
}

} // namespace

PlaylistImportResult ImportPlaylistCsv(const String& data, const String& source_name)
{
    PlaylistImportResult result;
    result.document.name = GetFileTitle(source_name);
    result.document.source_path = source_name;

    int nl = data.Find('\n');
    String first_line = nl >= 0 ? data.Left(nl) : data;
    Vector< Vector<String> > rows = ParseDelimited(data, DetectDelimiter(first_line), result.warnings);
    if(rows.IsEmpty()) {
        result.warnings.Add("No playlist rows were found.");
        return result;
    }

    bool header_present = LooksLikeHeader(rows[0]);
    Vector<String> header;
    int first_data = 0;
    if(header_present) {
        header = clone(rows[0]);
        first_data = 1;
    }

    int title_col = header_present ? FindColumn(header, { "title", "track", "song", "track_name", "song_title" }) : 0;
    int artist_col = header_present ? FindColumn(header, { "artist", "artists", "artist_name" }) : 1;
    int album_col = header_present ? FindColumn(header, { "album", "album_name" }) : 2;
    int isrc_col = header_present ? FindColumn(header, { "isrc" }) : 3;
    int uri_col = header_present ? FindColumn(header, { "spotify_uri", "uri", "spotify" }) : 4;

    if(title_col < 0) {
        result.warnings.Add("CSV has no title/track/song column.");
        return result;
    }

    for(int i = first_data; i < rows.GetCount(); i++) {
        TrackEntry entry = EntryFromCells(rows[i], title_col, artist_col, album_col, isrc_col, uri_col);
        if(entry.requested_title.IsEmpty() && entry.spotify_uri.IsEmpty()) {
            result.warnings.Add(Format("Skipped row %d because it has neither title nor Spotify URI.", i + 1));
            continue;
        }
        result.document.tracks.Add(pick(entry));
    }
    result.document.dirty = false;
    return result;
}

PlaylistImportResult ImportPlaylistText(const String& data, const String& source_name)
{
    PlaylistImportResult result;
    result.document.name = GetFileTitle(source_name);
    result.document.source_path = source_name;

    Vector<String> lines = Split(data, '\n');
    for(int i = 0; i < lines.GetCount(); i++) {
        String line = TrimBoth(lines[i]);
        if(line.IsEmpty())
            continue;

        TrackEntry entry;
        int tab = line.Find('\t');
        int dash = line.Find(" - ");
        if(tab >= 0) {
            entry.requested_title = TrimBoth(line.Left(tab));
            entry.requested_artist = TrimBoth(line.Mid(tab + 1));
        }
        else if(dash > 0) {
            entry.requested_title = TrimBoth(line.Left(dash));
            entry.requested_artist = TrimBoth(line.Mid(dash + 3));
        }
        else
            entry.requested_title = line;

        result.document.tracks.Add(pick(entry));
    }

    if(result.document.tracks.IsEmpty())
        result.warnings.Add("No non-empty song lines were found.");
    return result;
}

String ExportPlaylistCsv(const PlaylistDocument& document)
{
    String out = "title,artist,album,isrc,spotify_uri\r\n";
    for(const TrackEntry& entry : document.tracks) {
        const SpotifyTrack *resolved = entry.GetResolved();
        String title = resolved ? resolved->title : entry.requested_title;
        String artist = resolved ? resolved->artist : entry.requested_artist;
        String album = resolved ? resolved->album : entry.requested_album;
        String isrc = resolved ? resolved->isrc : entry.requested_isrc;
        out << CsvQuote(title) << ','
            << CsvQuote(artist) << ','
            << CsvQuote(album) << ','
            << CsvQuote(isrc) << ','
            << CsvQuote(entry.ResolvedUri()) << "\r\n";
    }
    return out;
}

} // namespace Upp
