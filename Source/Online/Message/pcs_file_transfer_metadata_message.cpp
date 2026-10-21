//-----------------------------------------------------------------------------
//           Name: pcs_file_transfer_metadata_message.cpp
//      Developer: Wolfire Games LLC
//    Description:
//        License: Read below
//-----------------------------------------------------------------------------
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
#include "pcs_file_transfer_metadata_message.h"

#include <Online/online.h>
#include <Online/online_utility.h>
#include <Online/Message/pcs_loading_completed_message.h>

#include <Main/engine.h>
#include <Internal/modloading.h>
#include <Utility/binn_util.h>

#include <string>

namespace OnlineMessages {
PCSFileTransferMetadataMessage::PCSFileTransferMetadataMessage(const std::string& map_name, const std::string& campaign_id, bool preload)
    : OnlineMessageBase(OnlineMessageCategory::TRANSIENT), map_name(map_name), campaign_id(campaign_id), preload(preload) {
}

binn* PCSFileTransferMetadataMessage::Serialize(void* object) {
    PCSFileTransferMetadataMessage* t = static_cast<PCSFileTransferMetadataMessage*>(object);
    binn* l = binn_object();

    binn_object_set_std_string(l, "map_name", t->map_name);
    binn_object_set_std_string(l, "campaign_id", t->campaign_id);
    binn_object_set_bool(l, "preload", t->preload);

    return l;
}

void PCSFileTransferMetadataMessage::Deserialize(void* object, binn* l) {
    PCSFileTransferMetadataMessage* t = static_cast<PCSFileTransferMetadataMessage*>(object);

    binn_object_get_std_string(l, "map_name", &t->map_name);
    binn_object_get_std_string(l, "campaign_id", &t->campaign_id);
    BOOL preload = FALSE;
    binn_object_get_bool(l, "preload", &preload);
    t->preload = preload != FALSE;
}

void PCSFileTransferMetadataMessage::Execute(const OnlineMessageRef& ref, void* object, PeerID from) {
    PCSFileTransferMetadataMessage* t = static_cast<PCSFileTransferMetadataMessage*>(object);

    // We now know what level the host wants us to load. Host hasn't actually send us the level for now.
    // This is placeholder behaviour, as we'd now want to wait for the host to send us a bunch of files
    // Due to the required refactor for this behaviour, we can just start loading what we assume is the map the host would have sent us.

    Online* online = Online::Instance();

    // Overgrowth Online: the host told us as it started its own load and we
    // loaded together; this is the handshake's copy: we are on it (or still
    // loading it, and say so when done)
    if (!t->preload && online->preloaded_level == t->map_name) {
        online->preloaded_level.clear();
        if (online->preload_loaded) {
            online->Send<OnlineMessages::PCSLoadingCompletedMessage>();
        }
        return;
    }
    online->preloaded_level = t->preload ? t->map_name : "";
    online->preload_loaded = false;
    // Overgrowth Online: a new level, not started until the host says so. The
    // previous level's start was still set: a guest that loaded first walked
    // into the level before the host had it (empty map, no character) instead
    // of waiting on its loading screen
    online->SessionStarted(false);

    Online::Instance()->level_name = t->map_name;
    // Overgrowth Online: the host's campaign, ours too (co-op rules: a guest knew
    // none, so it never counted as a story level)
    Online::Instance()->campaign_id = t->campaign_id;
    if (ModLoading::Instance().WhichCampaignLevelBelongsTo(t->map_name) != "") {
        Online::Instance()->online_session->free_avatars.clear();

        Engine::Instance()->ScriptableUICallback("set_campaign " + t->campaign_id);
        Engine::Instance()->ScriptableUICallback(t->map_name);
    } else {
        LOGE << "Unabled to load level: \"" + t->map_name + "\" in campaign: " + t->campaign_id << std::endl;
        Online::Instance()->QueueStopMultiplayer();
        Engine::Instance()->QueueErrorMessage("Error", "Unabled to load level: \"" + t->map_name + "\" in campaign: " + t->campaign_id);
    }
}

void* PCSFileTransferMetadataMessage::Construct(void* mem) {
    return new (mem) PCSFileTransferMetadataMessage("", "", false);
}

void PCSFileTransferMetadataMessage::Destroy(void* object) {
    PCSFileTransferMetadataMessage* t = static_cast<PCSFileTransferMetadataMessage*>(object);
    t->~PCSFileTransferMetadataMessage();
}
}  // namespace OnlineMessages
