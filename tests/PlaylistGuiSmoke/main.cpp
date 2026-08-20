#include <CtrlLib/CtrlLib.h>

using namespace Upp;

class PlaylistGuiSmokeWindow : public TopWindow {
public:
    PlaylistGuiSmokeWindow()
    {
        Title("PlaylistLab GUI Smoke");
        Sizeable().Zoomable();
        SetRect(0, 0, DPI(520), DPI(220));
    }
};

GUI_APP_MAIN
{
    PlaylistGuiSmokeWindow window;
    window.OpenMain();

    String report;
    report << "PlaylistLab GUI startup smoke\n"
           << "open=" << (window.IsOpen() ? 1 : 0) << '\n'
           << "visible=" << (window.IsVisible() ? 1 : 0) << '\n';
    SaveFile(ConfigFile("playlistlab-gui-smoke.txt"), report);

    window.Run();
}
