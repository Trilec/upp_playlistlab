#ifndef _PlaylistWorkbench_PlaylistIO_h_
#define _PlaylistWorkbench_PlaylistIO_h_

#include "PlaylistModel.h"

namespace Upp {

struct PlaylistImportResult {
    PlaylistDocument document;
    Vector<String>    warnings;
};

PlaylistImportResult ImportPlaylistCsv(const String& data, const String& source_name = Null);
PlaylistImportResult ImportPlaylistText(const String& data, const String& source_name = Null);
String               ExportPlaylistCsv(const PlaylistDocument& document);

} // namespace Upp

#endif
