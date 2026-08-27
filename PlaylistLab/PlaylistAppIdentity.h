#ifndef _PlaylistLab_PlaylistAppIdentity_h_
#define _PlaylistLab_PlaylistAppIdentity_h_

#include <CtrlLib/CtrlLib.h>

namespace Upp {

// Returns the single embedded PlaylistLab application identity image.
// The PNG is compiled into the package so window/title-card identity never
// depends on the repository layout or the executable's launch directory.
Image PlaylistLabAppIcon();

}

#endif
