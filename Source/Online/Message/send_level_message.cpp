//-----------------------------------------------------------------------------
//           Name: send_level_message.cpp
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
#include "send_level_message.h"

#include <Main/engine.h>
#include <Game/level.h>
#include <Online/online.h>

namespace OnlineMessages {
SendLevelMessage::SendLevelMessage(string msg) : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT),
                                                 msg(msg) {
}

binn* SendLevelMessage::Serialize(void* object) {
    binn* l = binn_object();

    SendLevelMessage* lm = static_cast<SendLevelMessage*>(object);

    binn_object_set_std_string(l, "msg", lm->msg);

    return l;
}

void SendLevelMessage::Deserialize(void* object, binn* l) {
    SendLevelMessage* lm = static_cast<SendLevelMessage*>(object);

    binn_object_get_std_string(l, "msg", &lm->msg);
}

void SendLevelMessage::Execute(const OnlineMessageRef& ref, void* object, PeerID peer) {
    SendLevelMessage* lm = static_cast<SendLevelMessage*>(object);

    SceneGraph* sg = Engine::Instance()->GetSceneGraph();

    if (sg != nullptr) {
        Online* online = Online::Instance();
        if (online->IsHosting()) {
            // Overgrowth Online: from a client only its level scripts' requests, tagged with who asked
            if (lm->msg.compare(0, 4, "ohr ") == 0) {
                online->delivering_network_level_message = true;
                sg->level->Message("ohr " + std::to_string((int)peer) + " " + online->TranslateIDs(lm->msg.substr(4), false));
                online->delivering_network_level_message = false;
            }
        } else if (lm->msg.compare(0, 10, "ohg speed ") == 0) {
            Engine::Instance()->FollowGameSpeed((float)atof(lm->msg.c_str() + 10));  // the host's game speed
        } else if (lm->msg.compare(0, 11, "ohg revive ") == 0) {
            online->CoopReviveRequest(lm->msg);  // a checkpoint: our character back up
        } else if (lm->msg.compare(0, 4, "ohl ") != 0 && (lm->msg.find(' ') != std::string::npos || lm->msg.find("reset") == std::string::npos)) {
            // Overgrowth Online: from the network only the level channel and the
            // host's reset (never "loadlevel <path>", "go_to_main_menu", ...)
            LOGW << "Overgrowth Online: a level message from the host refused: " << lm->msg.substr(0, 40) << std::endl;
        } else {
            online->delivering_network_level_message = true;  // (the host's reset counts here)
            sg->level->Message(lm->msg.compare(0, 4, "ohl ") == 0 ? online->TranslateIDs(lm->msg, false) : lm->msg);
            online->delivering_network_level_message = false;
        }
    } else {
        LOGW << "Received level message: \"" << lm->msg << "\" but SceneGraph is nullptr" << std::endl;
    }
}

void* SendLevelMessage::Construct(void* mem) {
    return new (mem) SendLevelMessage("");
}

void SendLevelMessage::Destroy(void* object) {
    SendLevelMessage* lm = static_cast<SendLevelMessage*>(object);
    lm->~SendLevelMessage();
}
}  // namespace OnlineMessages
