#ifndef _PlaylistLab_PlaylistHelp_h_
#define _PlaylistLab_PlaylistHelp_h_

#include <CtrlLib/CtrlLib.h>

namespace Upp {

// Opens the self-contained Spotify setup/usage guide. The guide deliberately
// links to Spotify's own pages rather than attempting to discover Client IDs or
// credentials on the user's behalf.
void ShowPlaylistHelp();

} // namespace Upp

#endif
