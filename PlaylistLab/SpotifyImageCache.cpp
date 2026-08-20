#include "SpotifyImageCache.h"

namespace Upp {

String SpotifyImageCache::SafeKey(const String& key)
{
    String out;
    out.Reserve(key.GetCount());
    for(int i = 0; i < key.GetCount(); i++) {
        int c = (byte)key[i];
        out.Cat(IsAlNum(c) || c == '-' || c == '_' ? c : '_');
    }
    return out.IsEmpty() ? String("unknown") : out;
}

String SpotifyImageCache::GetCacheDirectory()
{
    String dir = AppendFileName(GetFileFolder(ConfigFile("playlistlab.spotify.json")),
                                "playlistlab-cache");
    RealizeDirectory(dir);
    return dir;
}

String SpotifyImageCache::CachePath(const String& key)
{
    return AppendFileName(GetCacheDirectory(), "spotify-" + SafeKey(key) + ".image");
}

Image SpotifyImageCache::Load(const String& key)
{
    String path = CachePath(key);
    if(!FileExists(path))
        return Image();
    Image image = StreamRaster::LoadFileAny(path);
    if(IsNull(image))
        DeleteFile(path);
    return image;
}

Image SpotifyImageCache::LoadOrFetch(const String& key, const String& url, String *error)
{
    if(error)
        error->Clear();

    Image cached = Load(key);
    if(!IsNull(cached))
        return cached;
    if(url.IsEmpty())
        return Image();

    HttpRequest request(url);
    request.Accept("image/*").RequestTimeout(20000);
    String data = request.Execute();
    if(request.IsSocketError()) {
        if(error)
            *error = "Artwork download failed: " + request.GetErrorDesc();
        return Image();
    }
    int status = request.GetStatusCode();
    if(status < 200 || status >= 300) {
        if(error)
            *error = Format("Artwork download returned HTTP %d.", status);
        return Image();
    }

    Image image = StreamRaster::LoadStringAny(data);
    if(IsNull(image)) {
        if(error)
            *error = "Artwork download was not a supported image.";
        return Image();
    }

    String path = CachePath(key);
    if(!SaveFile(path, data) && error)
        *error = "Artwork loaded but could not be cached locally.";
    return image;
}

} // namespace Upp
