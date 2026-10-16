"""Clients: other players' avatars get a client-side virtual controller and
camera instead of keeping the level's single-player set-up (controller 0,
camera 0), which made them read the client's input and fight its avatar for
the camera (the shaking camera). Run from the repo root."""
S = "Source/Game/"
MARK = "//    Modified By: Overhaul Online (crxhvrd)\n"


def edit(path, pairs):
    s = open(S + path, encoding="utf-8").read()
    for old, new in pairs:
        assert s.count(old) == 1, (path, old[:80], s.count(old))
        s = s.replace(old, new)
    if MARK not in s and "//      Developer: Wolfire Games LLC\n" in s:
        s = s.replace("//      Developer: Wolfire Games LLC\n", "//      Developer: Wolfire Games LLC\n" + MARK, 1)
    open(S + path, "w", encoding="utf-8", newline="").write(s)


edit("avatar_control_manager.h", [
    ("""#include <vector>""", """#include <map>
#include <vector>"""),
    ("""    std::vector<ObjectID> GetUnpossessedAvatars();
""", """    std::vector<ObjectID> GetUnpossessedAvatars();

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
"""),
])

edit("avatar_control_manager.cpp", [
    ("""#include <Online/online.h>
""", """#include <Online/online.h>
#include <UserInput/input.h>

AvatarControlManager::RemoteSlot AvatarControlManager::ClientRemoteSlot(ObjectID avatar) {
    RemoteSlot& slot = client_remote_slots_[avatar];

    if (slot.controller_id == -1) {
        slot.controller_id = Input::Instance()->AllocateRemotePlayerInput();
        slot.camera_id = ActiveCameras::Instance()->CreateVirtualCameraInstance();
    }

    return slot;
}
"""),
    ("""            } else {
                // Virtual controller_id, used for syncing input from remote source
                camera_id = player_state.camera_id;
                controller_id = player_state.controller_id;
            }""", """            } else if (online->IsClient()) {
                // Another player's avatar on a client: our own virtual slot
                RemoteSlot slot = ClientRemoteSlot(player_state.object_id);
                camera_id = slot.camera_id;
                controller_id = slot.controller_id;
            } else {
                // Virtual controller_id, used for syncing input from remote source
                camera_id = player_state.camera_id;
                controller_id = player_state.controller_id;
            }"""),
    ("""    for (int i = 0; i < unpossessed_avatars.size(); i++) {
        int camera_id = 0;

        if (Engine::Instance()->GetSplitScreen()) {
            camera_id = i;
        }

        possessions.push_back(Possession(unpossessed_avatars[i], local_player_index, camera_id, !Online::Instance()->IsActive() || Online::Instance()->IsHosting()));
        local_player_index++;
    }""", """    for (int i = 0; i < unpossessed_avatars.size(); i++) {
        if (online->IsClient()) {
            // Not ours and no player's: the host runs it, keep it off our input and camera
            RemoteSlot slot = ClientRemoteSlot(unpossessed_avatars[i]);
            possessions.push_back(Possession(unpossessed_avatars[i], slot.controller_id, slot.camera_id, false));
            continue;
        }

        int camera_id = 0;

        if (Engine::Instance()->GetSplitScreen()) {
            camera_id = i;
        }

        possessions.push_back(Possession(unpossessed_avatars[i], local_player_index, camera_id, !Online::Instance()->IsActive() || Online::Instance()->IsHosting()));
        local_player_index++;
    }"""),
])

# Clients: an avatar already set up as someone else's is not "unpossessed"
# just because the level made it controlled: GetUnpossessedAvatars uses
# IsAvatarPossessed (controlled flag), which would hide the host's avatar.
print("ok")
