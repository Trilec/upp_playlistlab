#include "SpotifyAuth.h"

namespace Upp {

const int SpotifyAuth::CALLBACK_PORT;

namespace {

const char *AUTH_URL  = "https://accounts.spotify.com/authorize";
const char *TOKEN_URL = "https://accounts.spotify.com/api/token";
const char *SCOPES = "playlist-read-private playlist-read-collaborative playlist-modify-private playlist-modify-public user-read-private";

String AuthConfigPath()
{
    return ConfigFile("playlistlab.spotify.json");
}

String MapString(const ValueMap& map, const char *key)
{
    Value v = map[key];
    return IsNull(v) || v.IsVoid() ? String() : AsString(v);
}

} // namespace

SpotifyAuth::SpotifyAuth()
{
    Load();
}

void SpotifyAuth::SetClientId(const String& id)
{
    String next = TrimBoth(id);
    if(next != client_id) {
        client_id = next;
        access_token.Clear();
        refresh_token.Clear();
        granted_scope.Clear();
    }
}

String SpotifyAuth::GetRedirectUri() const
{
    return Format("http://127.0.0.1:%d/callback", CALLBACK_PORT);
}

String SpotifyAuth::RandomUrlSafe(int count)
{
    static const char chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    String out;
    for(int i = 0; i < count; i++)
        out.Cat(chars[Random((int)strlen(chars))]);
    return out;
}

String SpotifyAuth::Base64Url(const byte *data, int count)
{
    String out = Base64Encode((const char *)data, count);
    out.Replace("+", "-");
    out.Replace("/", "_");
    while(out.EndsWith("="))
        out.Trim(out.GetCount() - 1);
    return out;
}

String SpotifyAuth::PkceChallenge(const String& verifier)
{
    byte hash[32];
    SHA256(hash, verifier);
    return Base64Url(hash, 32);
}

String SpotifyAuth::FormField(const String& key, const String& value)
{
    return UrlEncode(key) + "=" + UrlEncode(value);
}

String SpotifyAuth::QueryValue(const String& uri, const String& key)
{
    int q = uri.Find('?');
    if(q < 0)
        return String();
    Vector<String> parts = Split(uri.Mid(q + 1), '&');
    for(const String& part : parts) {
        int eq = part.Find('=');
        String name = UrlDecode(eq < 0 ? part : part.Left(eq));
        if(name == key)
            return UrlDecode(eq < 0 ? String() : part.Mid(eq + 1));
    }
    return String();
}

bool SpotifyAuth::Load()
{
    String json = LoadFile(AuthConfigPath());
    if(json.IsEmpty())
        return false;
    try {
        Value v = ParseJSON(json);
        if(!IsValueMap(v))
            return false;
        ValueMap map = v;
        client_id = MapString(map, "client_id");
        refresh_token = MapString(map, "refresh_token");
        granted_scope = MapString(map, "scope");
        access_token.Clear();
        return !client_id.IsEmpty();
    }
    catch(...) {
        return false;
    }
}

bool SpotifyAuth::Save() const
{
    Json json;
    json("client_id", client_id)
        ("refresh_token", refresh_token)
        ("scope", granted_scope);
    return SaveFile(AuthConfigPath(), ~json);
}

void SpotifyAuth::Disconnect()
{
    access_token.Clear();
    refresh_token.Clear();
    granted_scope.Clear();
    last_error.Clear();
    Save();
}

bool SpotifyAuth::ReadTokenResponse(HttpRequest& request, bool keep_existing_refresh)
{
    String body = request.Execute();
    if(request.IsSocketError()) {
        last_error = "Spotify token request failed: " + request.GetErrorDesc();
        return false;
    }

    Value parsed;
    try {
        parsed = ParseJSON(body);
    }
    catch(...) {
        last_error = Format("Spotify token endpoint returned HTTP %d with an unreadable response.", request.GetStatusCode());
        return false;
    }
    if(!IsValueMap(parsed)) {
        last_error = "Spotify token endpoint returned an unexpected response.";
        return false;
    }

    ValueMap map = parsed;
    if(request.GetStatusCode() < 200 || request.GetStatusCode() >= 300) {
        String code = MapString(map, "error");
        String detail = MapString(map, "error_description");
        last_error = "Spotify authorization failed";
        if(!code.IsEmpty())
            last_error << ": " << code;
        if(!detail.IsEmpty())
            last_error << " — " << detail;
        if(code == "invalid_grant") {
            access_token.Clear();
            refresh_token.Clear();
            Save();
        }
        return false;
    }

    String next_access = MapString(map, "access_token");
    if(next_access.IsEmpty()) {
        last_error = "Spotify token response did not contain an access token.";
        return false;
    }

    access_token = next_access;
    String next_refresh = MapString(map, "refresh_token");
    if(!next_refresh.IsEmpty())
        refresh_token = next_refresh;
    else if(!keep_existing_refresh)
        refresh_token.Clear();
    granted_scope = MapString(map, "scope");
    last_error.Clear();
    Save();
    return true;
}

bool SpotifyAuth::ExchangeAuthorizationCode(const String& code, const String& verifier)
{
    String body;
    body << FormField("client_id", client_id)
         << '&' << FormField("grant_type", "authorization_code")
         << '&' << FormField("code", code)
         << '&' << FormField("redirect_uri", GetRedirectUri())
         << '&' << FormField("code_verifier", verifier);

    HttpRequest request(TOKEN_URL);
    request.ContentType("application/x-www-form-urlencoded")
           .Post(body)
           .RequestTimeout(30000);
    return ReadTokenResponse(request, false);
}

bool SpotifyAuth::RefreshAccessToken()
{
    if(client_id.IsEmpty() || refresh_token.IsEmpty()) {
        last_error = "Spotify is not authorized yet.";
        return false;
    }

    String body;
    body << FormField("grant_type", "refresh_token")
         << '&' << FormField("refresh_token", refresh_token)
         << '&' << FormField("client_id", client_id);

    HttpRequest request(TOKEN_URL);
    request.ContentType("application/x-www-form-urlencoded")
           .Post(body)
           .RequestTimeout(30000);
    return ReadTokenResponse(request, true);
}

bool SpotifyAuth::EnsureAccessToken()
{
    return !access_token.IsEmpty() || RefreshAccessToken();
}

bool SpotifyAuth::AuthorizeInteractive()
{
    last_error.Clear();
    if(client_id.IsEmpty()) {
        last_error = "Enter the Spotify Client ID before connecting.";
        return false;
    }

    IpAddrInfo loopback;
    if(!loopback.Execute("127.0.0.1", CALLBACK_PORT, IpAddrInfo::FAMILY_IPV4)) {
        last_error = "Cannot resolve the local Spotify callback address.";
        return false;
    }

    TcpSocket server;
    server.Timeout(120000);
    if(!server.Listen(loopback, CALLBACK_PORT)) {
        last_error = Format("Cannot listen on 127.0.0.1:%d for the Spotify callback: %s",
                            CALLBACK_PORT, server.GetErrorDesc());
        return false;
    }

    String verifier = RandomUrlSafe(64);
    String state = RandomUrlSafe(32);
    String url;
    url << AUTH_URL
        << "?client_id=" << UrlEncode(client_id)
        << "&response_type=code"
        << "&redirect_uri=" << UrlEncode(GetRedirectUri())
        << "&state=" << UrlEncode(state)
        << "&scope=" << UrlEncode(SCOPES)
        << "&code_challenge_method=S256"
        << "&code_challenge=" << UrlEncode(PkceChallenge(verifier));

    LaunchWebBrowser(url);

    TcpSocket client;
    if(!client.Accept(server)) {
        last_error = "Spotify callback connection was not accepted: " + client.GetErrorDesc();
        return false;
    }

    HttpHeader header;
    if(!header.Read(client)) {
        last_error = "Spotify callback did not contain a valid HTTP request.";
        HttpResponse(client, false, 400);
        return false;
    }

    String uri = header.GetURI();
    String returned_state = QueryValue(uri, "state");
    String error = QueryValue(uri, "error");
    String code = QueryValue(uri, "code");

    if(returned_state != state) {
        last_error = "Spotify callback state did not match the authorization request.";
        HttpResponse(client, false, 400, nullptr, "text/html",
                     "<html><body><h2>PlaylistLab authorization failed.</h2><p>You can close this tab.</p></body></html>");
        return false;
    }
    if(!error.IsEmpty()) {
        last_error = "Spotify authorization was declined: " + error;
        HttpResponse(client, false, 400, nullptr, "text/html",
                     "<html><body><h2>PlaylistLab was not authorized.</h2><p>You can close this tab.</p></body></html>");
        return false;
    }
    if(code.IsEmpty()) {
        last_error = "Spotify callback did not contain an authorization code.";
        HttpResponse(client, false, 400, nullptr, "text/html",
                     "<html><body><h2>PlaylistLab authorization failed.</h2><p>You can close this tab.</p></body></html>");
        return false;
    }

    HttpResponse(client, false, 200, nullptr, "text/html",
                 "<html><body><h2>PlaylistLab is connected to Spotify.</h2><p>You can close this tab and return to the application.</p></body></html>");
    return ExchangeAuthorizationCode(code, verifier);
}

} // namespace Upp
