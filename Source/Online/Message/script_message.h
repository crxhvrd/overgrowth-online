//-----------------------------------------------------------------------------
//           Name: script_message.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: The script API's messages (Online_Send, Online_SendToHost,
//                 Online_SendToPlayer): text on a named channel between the
//                 scripts of the session's machines. Guests send to the host,
//                 which delivers it, stamps the sender and passes it on.
//                 Docs/Markdown/Overgrowth Online Script API.md
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

#include "online_message_base.h"

#include <cstdint>
#include <string>

namespace OnlineMessages {
class ScriptMessage : public OnlineMessageBase {
   public:
    static const int32_t kEveryone = -1;  // every other player
    static const int32_t kDropped = -2;   // (the host refused it: not delivered, not passed on)

    int32_t to;    // a player (the script API's ids: the host is 0) or kEveryone
    int32_t from;  // the sender, as the host knows it
    std::string channel;
    std::string text;

    ScriptMessage(int32_t to, int32_t from, const std::string& channel, const std::string& text);

    static binn* Serialize(void* object);
    static void Deserialize(void* object, binn* source);
    static void Execute(const OnlineMessageRef& ref, void* object, PeerID from);
    static void* Construct(void* mem);
    static void Destroy(void* object);
};
}  // namespace OnlineMessages
