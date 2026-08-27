#include "PlaylistHelp.h"

#include <Ui/Ui.h>

namespace Upp {
namespace {

const char *SPOTIFY_DASHBOARD_URL = "https://developer.spotify.com/dashboard";
const char *SPOTIFY_ACCOUNT_APPS_URL = "https://www.spotify.com/nz/account/apps/";
const char *SPOTIFY_2026_CHANGES_URL = "https://developer.spotify.com/documentation/web-api/references/changes/february-2026";
const char *PLAYLISTLAB_REDIRECT_URI = "http://127.0.0.1:43821/callback";

String HelpText()
{
    return String()
        << "SPOTIFY SETUP\n"
        << "PlaylistLab talks to Spotify through Spotify's Web API. Before PlaylistLab can load or change your playlists, you need a Spotify Developer app and its Client ID. PlaylistLab never needs or stores your Client Secret.\n\n"
        << "WHAT YOU NEED\n"
        << "1. A Spotify account.\n"
        << "2. Spotify Premium if your app is in Development Mode. Spotify currently requires the app owner to have an active Premium subscription for Development Mode apps to function.\n"
        << "3. A Spotify Developer app created in your own Spotify Developer Dashboard.\n"
        << "4. The Client ID shown on that app's Basic Information page.\n"
        << "5. This redirect URI registered exactly in the app settings:\n"
        << "   " << PLAYLISTLAB_REDIRECT_URI << "\n\n"
        << "HOW TO GET YOUR CLIENT ID\n"
        << "1. First sign in to Spotify in your normal web browser. If you are not signed in, the developer pages may not show your apps correctly.\n"
        << "2. Open the Spotify Developer Dashboard using the button below.\n"
        << "3. Choose Create app, or open the PlaylistLab app you already created.\n"
        << "4. Give it a useful name such as PlaylistLab.\n"
        << "5. In the app settings, register the redirect URI shown above exactly.\n"
        << "6. Open the app's Basic Information page and copy its Client ID.\n"
        << "7. Back in PlaylistLab, press + beside the Client profile selector. Give the profile a friendly name and paste the Client ID.\n"
        << "8. Press Refresh/Authorize. Spotify will open a browser authorization page when required.\n\n"
        << "The Spotify Account Apps page is useful for reviewing apps connected to your Spotify account, but it is not where the Developer Client ID is shown. The Client ID comes from the Spotify Developer Dashboard.\n\n"
        << "AUTHORIZATION LIFETIME\n"
        << "The Client ID itself does not expire. Spotify now gives user refresh tokens a six-month lifetime (about 180 days). Refreshing an access token does not extend that six-month period. When the refresh token expires, PlaylistLab must send you through Spotify authorization again. Your app and Client ID remain the same.\n\n"
        << "SPOTIFY'S 2026 DEVELOPMENT-MODE CHANGES\n"
        << "Spotify changed Development Mode in 2026. Development Mode requires Premium for the app owner, has user/quota limits, and uses a reduced/changed Web API surface. PlaylistLab deliberately uses the current playlist endpoints (for example /me/playlists and /playlists/{id}/items) rather than retired playlist-track endpoints.\n\n"
        << "PLAYLISTLAB BASICS\n"
        << "Selected Spotify Playlist — the playlist currently being viewed in Spotify. If Spotify allows item access, its tracks can be copied or dragged into Working.\n\n"
        << "Working Playlist — the local playlist you are building. Find, CSV import, pasted text and Spotify tracks all feed Working. Working is the only list PlaylistLab publishes back to Spotify.\n\n"
        << "Resolve — searches Spotify for unresolved imported/pasted rows. Strong unique matches can be linked automatically. Ambiguous rows remain available for Review Match.\n\n"
        << "Create New — creates a new private Spotify playlist in exact Working order.\n\n"
        << "Append Missing — appends only Working track occurrences not already represented in the selected editable Spotify playlist. It does not reorder or delete existing Spotify items.\n\n"
        << "Replace — destructive. It replaces the selected editable Spotify playlist with the exact Working order. PlaylistLab always shows the exact preview first.\n\n"
        << "Open in Spotify — opens the selected track or playlist in Spotify. It is navigation, not playback control. PlaylistLab can add true Spotify playback control separately; that requires Spotify's player-control permission and a Premium playback device.\n\n"
        << "TRACK DETAILS AND NOTES\n"
        << "Selecting a track in either Selected Spotify Playlist or Working Playlist updates Track Details. Working-track notes are local PlaylistLab metadata and are never sent to Spotify. If a Spotify source track already has a matching Working copy with a local note, PlaylistLab can show that note while you inspect the Spotify track.\n\n"
        << "TROUBLESHOOTING\n"
        << "I cannot find the Client ID — make sure you are signed in to Spotify in the browser, then open the Developer Dashboard and open the specific app. The normal Account Apps page does not expose the Developer Client ID.\n\n"
        << "Spotify asks me to authorize again — that is expected after the six-month refresh-token lifetime, or if Spotify revoked/invalidated the authorization. Re-authorize; do not create a new Client ID just because the token expired.\n\n"
        << "A playlist is visible in Spotify but PlaylistLab says META — Spotify's own client and third-party Web API do not have identical access rules. PlaylistLab attempts the real item request; a Spotify 403/404 means the API withheld the item list for this account/app.\n\n"
        << "A playlist says CHECK — PlaylistLab has its metadata but has not yet proved item access. Select it and PlaylistLab will try the real item endpoint.\n\n"
        << "Development Mode stopped working — confirm that the app owner's Premium subscription is active and check the Spotify Developer Dashboard for the app status and current platform restrictions.\n";
}

class PlaylistHelpDialog : public TopWindow {
public:
    typedef PlaylistHelpDialog CLASSNAME;

    PlaylistHelpDialog()
    {
        Title("PlaylistLab Help — Spotify Setup");
        Sizeable().Zoomable();
        SetRect(0, 0, DPI(820), DPI(720));
        SetMinSize(Size(DPI(680), DPI(560)));

        Add(title_); Add(body_); Add(button_row_);

        title_.SetTitle("PlaylistLab Help")
              .SetSubTitle("Spotify Client ID setup, authorization, workflow and troubleshooting")
              .SetMedia(ICON_DESIGN_HELP_48())
              .SetMediaSide(UiAlign::LEFT)
              .SetMediaAlign(UiAlign::CENTER, UiAlign::CENTER)
              .SetMediaAutoFit(true)
              .ShowTitleLine(false)
              .SetContentInset(DPI(8));

        // Help is intentionally a UiDoc rather than a plain multiline edit.
        // That gives the guide semantic headings and keeps presentation separate
        // from the underlying instructional text.
        LoadHelpDocument();
        body_.NoWantFocus();
        body_.ShowLineNumbers(false);
        body_.ShowMetadata(false);
        body_.Tip("PlaylistLab setup and usage guide. Use the buttons below to open Spotify's official pages.");

        button_row_.SetDirection(UiDirection::H).SetGap(DPI(6)).SetAlignItems(UiCrossAlign::Stretch);
        // These are escape hatches, not a second navigation system. Label them so
        // a first-time setup does not have to infer why five buttons suddenly appeared.
        button_row_.Add(links_label_).Fit();
        button_row_.Add(dashboard_).Fit();
        button_row_.Add(account_apps_).Fit();
        button_row_.Add(api_changes_).Fit();
        button_row_.Add(copy_redirect_).Fit();
        button_row_.AddSpacer(1);
        button_row_.Add(close_).Fit();

        links_label_.SetText("Helpful links");
        dashboard_.SetText("Developer Dashboard").Tip("Open Spotify Developer Dashboard. Sign in to Spotify in the browser first.");
        account_apps_.SetText("Account Apps").Tip("Open Spotify's connected-apps account page. This is not the page that shows the Developer Client ID.");
        api_changes_.SetText("2026 API Changes").Tip("Open Spotify's February 2026 Web API change log.");
        copy_redirect_.SetText("Copy Redirect URI").Tip("Copy PlaylistLab's required local callback URI to the clipboard.");
        close_.SetText("Close").Tip("Close PlaylistLab Help.");

        dashboard_.WhenAction = [=] { LaunchWebBrowser(SPOTIFY_DASHBOARD_URL); };
        account_apps_.WhenAction = [=] { LaunchWebBrowser(SPOTIFY_ACCOUNT_APPS_URL); };
        api_changes_.WhenAction = [=] { LaunchWebBrowser(SPOTIFY_2026_CHANGES_URL); };
        copy_redirect_.WhenAction = [=] {
            WriteClipboardText(PLAYLISTLAB_REDIRECT_URI);
            PromptOK("PlaylistLab redirect URI copied to the clipboard:\n\n" + String(PLAYLISTLAB_REDIRECT_URI));
        };
        close_.WhenAction = [=] { Close(); };

        ApplyTheme();
    }

    virtual void Paint(Draw& w) override
    {
        UiThemeMode mode = UiTheme::GetContext().mode;
        w.DrawRect(GetSize(), mode == UiThemeMode::Dark ? Color(18, 18, 18) : Color(247, 248, 250));
    }

    virtual void Layout() override
    {
        Rect rc = GetSize();
        int margin = DPI(12), gap = DPI(8);
        int width = max(0, rc.GetWidth() - margin * 2);
        int button_h = DPI(34);
        title_.SetRect(margin, margin, width, DPI(70));
        int body_y = margin + DPI(70) + gap;
        int button_y = max(body_y, rc.GetHeight() - margin - button_h);
        body_.SetRect(margin, body_y, width, max(0, button_y - body_y - gap));
        button_row_.SetRect(margin, button_y, width, button_h);
    }

private:
    void ApplyHeading(const String& heading, const String& role)
    {
        String text = body_.GetText();
        int at = text.Find(heading);
        if(at < 0)
            return;
        body_.SetSelection(UiDocRange(at, at + heading.GetCount()));
        body_.SetBlockRole(role);
    }

    void LoadHelpDocument()
    {
        body_.NewDocument();
        body_.SetText(HelpText());
        ApplyHeading("SPOTIFY SETUP", "heading.1");
        ApplyHeading("WHAT YOU NEED", "heading.2");
        ApplyHeading("HOW TO GET YOUR CLIENT ID", "heading.2");
        ApplyHeading("AUTHORIZATION LIFETIME", "heading.2");
        ApplyHeading("SPOTIFY'S 2026 DEVELOPMENT-MODE CHANGES", "heading.2");
        ApplyHeading("PLAYLISTLAB BASICS", "heading.2");
        ApplyHeading("TRACK DETAILS AND NOTES", "heading.2");
        ApplyHeading("TROUBLESHOOTING", "heading.2");
        body_.SetSelection(UiDocRange(0, 0));
    }

    void ApplyTheme()
    {
        UiThemeMode mode = UiTheme::GetContext().mode;
        bool dark = mode == UiThemeMode::Dark;
        title_.SetCustomStyle(UiTheme::ResolveTitleCard(UiThemePreset::Minimal, mode));
        links_label_.SetCustomStyle(UiTheme::ResolveLabel(UiThemePreset::Minimal, mode, UiLabelRole::Caption));

        UiDoc::Style doc = UiDoc::StyleDefault();
        Color page = dark ? Color(24, 24, 24) : White();
        Color frame = dark ? Color(62, 62, 62) : Color(210, 215, 222);
        Color ink = dark ? Color(224, 224, 224) : Color(24, 30, 38);
        Color disabled = dark ? Color(128, 128, 128) : Color(145, 150, 160);
        for(int i = 0; i < 4; i++) {
            doc.palette.face[i] = UiFill::Solid(page);
            doc.palette.frame[i] = frame;
            doc.palette.ink[i] = ink;
            doc.palette.icon[i] = ink;
        }
        doc.palette.ink[ST_DISABLED] = disabled;
        doc.palette.icon[ST_DISABLED] = disabled;
        doc.page_face = page;
        doc.page_frame = frame;
        doc.table_grid = frame;
        doc.caret_ink = ink;
        doc.selection_fill = dark ? Color(38, 73, 109) : Color(178, 215, 255);
        doc.search_fill = dark ? Color(94, 78, 27) : Color(255, 237, 158);
        doc.metrics.radius = DPI(6);
        doc.page_padding = DPI(18);
        body_.SetCustomStyle(doc);

        UiButton::Style standard = UiTheme::ResolveButton(UiThemePreset::Minimal, mode, UiButtonRole::Standard);
        UiButton::Style subtle = UiTheme::ResolveButton(UiThemePreset::Minimal, mode, UiButtonRole::Subtle);
        for(UiButton *button : { &dashboard_, &account_apps_, &api_changes_, &copy_redirect_ })
            button->SetCustomStyle(standard);
        close_.SetCustomStyle(subtle);
    }

    UiTitleCard title_;
    UiDoc body_;
    UiBoxLayout button_row_{UiDirection::H};
    UiLabel links_label_;
    UiButton dashboard_, account_apps_, api_changes_, copy_redirect_, close_;
};

} // namespace

void ShowPlaylistHelp()
{
    PlaylistHelpDialog dialog;
    dialog.Run();
}

} // namespace Upp
