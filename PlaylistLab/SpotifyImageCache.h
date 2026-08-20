#ifndef _PlaylistLab_SpotifyImageCache_h_
#define _PlaylistLab_SpotifyImageCache_h_

#include <CtrlCore/CtrlCore.h>

namespace Upp {

class SpotifyImageCache {
public:
    static Image Load(const String& key);
    static Image LoadOrFetch(const String& key, const String& url, String *error = nullptr);
    static String GetCacheDirectory();

private:
    static String CachePath(const String& key);
    static String SafeKey(const String& key);
};

} // namespace Upp

#endif
