#ifndef _PlaylistLab_PlaylistLocalState_h_
#define _PlaylistLab_PlaylistLocalState_h_

#include "PlaylistModel.h"

namespace Upp {

struct SpotifyClientProfile : Moveable<SpotifyClientProfile> {
    String name;
    String client_id;
};

class PlaylistLocalState {
public:
    static String ProfilesPath();
    static String WorkingPath();

    static bool LoadProfiles(Vector<SpotifyClientProfile>& profiles,
                             int& selected_profile,
                             String *error = nullptr);
    static bool SaveProfiles(const Vector<SpotifyClientProfile>& profiles,
                             int selected_profile,
                             String *error = nullptr);

    static bool LoadWorking(PlaylistDocument& document,
                            String& source_label,
                            String *error = nullptr);
    static bool SaveWorking(const PlaylistDocument& document,
                            const String& source_label,
                            String *error = nullptr);
};

} // namespace Upp

#endif
