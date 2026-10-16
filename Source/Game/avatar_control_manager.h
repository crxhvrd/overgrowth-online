//-----------------------------------------------------------------------------
//           Name: avatar_control_manager.h
//      Developer: Wolfire Games LLC
//    Modified By: Overgrowth Online (crxhvrd)
//    Description:
//        License: Read below
//-----------------------------------------------------------------------------
//
//
//   Copyright 2022 Wolfire Games LLC
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//
//-----------------------------------------------------------------------------
#pragma once

#include <Objects/object.h>

#include <map>
#include <vector>

class Possession {
   public:
    ObjectID avatar;
    int controller_id;
    int camera_id;
    bool is_local;

    Possession(ObjectID avatar, int controller_id, int camera_id, bool is_local) : avatar(avatar), controller_id(controller_id), camera_id(camera_id), is_local(is_local){};
};

// The AvatarControlManager dynamically changes player avatars control states base on requested possesions and engine state.
// This is done because the player characters can get AI possesion when switching into an editor state, or other non-game states.
// But needs to correctly revert to the possesor on demand, both local and online.
class AvatarControlManager {
   private:
    // std::vector<std::pair<ObjectID, int>> possesions;

    // Get a list of what controller input controlsw what player avatar.
    std::vector<Possession> GeneratePossessionList();

    std::vector<ObjectID> GetUnpossessedAvatars();

    // Clients: a virtual controller (reads no input) and a virtual camera for
    // every avatar that is not ours. The host's ids in its player states mean
    // nothing here; without these, other players' avatars kept controller 0
    // and camera 0 from the level's single-player set-up.
    struct RemoteSlot {
        int controller_id = -1;
        int camera_id = -1;
    };
    std::map<ObjectID, RemoteSlot> client_remote_slots_;
    RemoteSlot ClientRemoteSlot(ObjectID avatar);

   public:
    // void DefinePlayerPossesion(ObjectID avatar, int controller_id);
    void Update();
};
