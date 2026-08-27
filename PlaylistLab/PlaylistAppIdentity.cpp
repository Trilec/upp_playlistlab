#include "PlaylistAppIdentity.h"
#include "PlaylistResources.brc"

#include <Ui/Ui.h>

namespace Upp {

Image PlaylistLabAppIcon()
{
    static Image icon = StreamRaster::LoadStringAny(String(playlistlab_icon_png, playlistlab_icon_png_length));
    return IsNull(icon) ? ICON_DESIGN_WIDGETS_48() : icon;
}

}
