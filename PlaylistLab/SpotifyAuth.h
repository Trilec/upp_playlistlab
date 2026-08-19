#ifndef _PlaylistLab_SpotifyAuth_h_
#define _PlaylistLab_SpotifyAuth_h_

#include <Core/Core.h>

namespace Upp {

class SpotifyAuth {
public:
    static const int CALLBACK_PORT = 43821;

    SpotifyAuth();

    void   SetClientId(const String& id);
    String GetClientId() const                         { return client_id; }
    String GetAccessToken() const                      { return access_token; }
    String GetRefreshToken() const                     { return refresh_token; }
    String GetRedirectUri() const;
    String GetLastError() const                        { return last_error; }
    bool   HasClientId() const                         { return !client_id.IsEmpty(); }
    bool   HasRefreshToken() const                     { return !refresh_token.IsEmpty(); }
    bool   HasAccessToken() const                      { return !access_token.IsEmpty(); }

    bool Load();
    bool Save() const;
    void Disconnect();

    bool AuthorizeInteractive();
    bool RefreshAccessToken();
    bool EnsureAccessToken();
    void InvalidateAccessToken()                       { access_token.Clear(); }

private:
    String client_id;
    String access_token;
    String refresh_token;
    String granted_scope;
    String last_error;

    static String RandomUrlSafe(int count);
    static String Base64Url(const byte *data, int count);
    static String PkceChallenge(const String& verifier);
    static String FormField(const String& key, const String& value);
    static String QueryValue(const String& uri, const String& key);

    bool ExchangeAuthorizationCode(const String& code, const String& verifier);
    bool ReadTokenResponse(HttpRequest& request, bool keep_existing_refresh);
};

} // namespace Upp

#endif
